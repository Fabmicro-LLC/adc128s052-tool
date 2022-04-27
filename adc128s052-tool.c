/*
	A simple tool designated to read data from ADC128 device using spidev API.
	This tool is based on spidev-test tool which is part of Linux kernel, please
	check it for more details.
	
	Copyright (C) 2022, Fabmicro, LLC., Tyumen, Russia. 
	Copyright (C) 2022, Ruslan Zalata <rz@fabmicro.ru>

	SPDX-License-Identifier: GPL-2.0-or-later

*/

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <byteswap.h>
#include <linux/types.h>
#include <spidev.h> 

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
	if (errno != 0)
		perror(s);
	else
		printf("%s\n", s);

	abort();
}

static const char *device = "/dev/spidev1.1";
static uint32_t mode;
static uint8_t bits = 8;
static char *channels = "01234567";
static char *output_file;
static uint32_t speed = 400000;
static int samples = 1;

static void usage(const char *prog)
{
	printf("ADC128 tool allows reading data from ADC in blocks using SPIDEV API\n"
	       "Copyright (C) 2022, Fabmicro, LLC., Ruslan Zalata <rz@fabmicro.ru>\n\n");
	printf("Usage: %s [-DsoCS]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.1)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -o --output   output data to a file (e.g. \"results.bin\")\n"
	     "  -C --channels  List of ADC channels (e.g. \"012345\" means channels from 0 to 5)\n"
	     "  -S --size     transfer size\n"
	     );
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "output",  1, 0, 'o' },
			{ "channels", 1, 0, 'C' },
			{ "samples",    1, 0, 'S' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "C:D:s:o:S:",
				lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 'C':
			channels = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'o':
			output_file = optarg;
			break;
		case 'S':
			samples = atoi(optarg);
			break;
		default:
			usage(argv[0]);
		}
	}
}



int main(int argc, char *argv[])
{
	uint64_t sum[16];
	uint16_t min[16];
	uint16_t max[16];
	int ret = 0;
	int fd;

	parse_opts(argc, argv);

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("Can't open device");


	// Set SPI mode

	ret = ioctl(fd, SPI_IOC_WR_MODE32, &mode);
	if (ret == -1)
		pabort("Can't set spi mode");

	ret = ioctl(fd, SPI_IOC_RD_MODE32, &mode);
	if (ret == -1)
		pabort("Can't get spi mode");

/*
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("Can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("Can't get bits per word");
*/


	// Set TX speed

	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("Can't set max speed hz");

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("Can't get max speed hz");

	printf("SPI max speed: %u Hz (%u kHz)\n", speed, speed/1000);

	struct timespec t1,t2;


	// Calculate block size depending on channels and samples

	int len = strlen(channels) * 2 * samples + 2; // one extra sample (2 bytes) is needed

	struct spi_ioc_transfer tr = {
		.len = len,
		.delay_usecs = 0,
		.speed_hz = speed,
		.bits_per_word = bits,
		.tx_nbits = 0,
	};

	if(!(tr.tx_buf = (unsigned long) malloc(len)))
		pabort("Cannot allocate tx_buf\n");

	if(!(tr.rx_buf = (unsigned long) malloc(len)))
		pabort("Cannot allocate rx_buf\n");


	// Fill ADC channel numbers inito TX block times samples

	unsigned short *ptr = (unsigned short*) tr.tx_buf;

	for(int i = 0; i < samples; i++)
		for(int j = 0; j < strlen(channels); j++)
			*ptr++ = (channels[j] - 0x30) << 3;	

	printf("Starting SPI transfer block of %d bytes (%d channels, %d samples)\n", len, strlen(channels), samples);

	clock_gettime(CLOCK_MONOTONIC, &t1);

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("Can't send spi message");

	clock_gettime(CLOCK_MONOTONIC, &t2);

	uint64_t interval = t2.tv_sec * 1000000000LL - t1.tv_sec * 1000000000LL + t2.tv_nsec - t1.tv_nsec;

	double tx_rate = 1.0 * (len * 8 * 1000000000LL / interval);

	printf("Effective transfer rate: %.1fkbps (%.1f kSamples/s\n", tx_rate / 1024, tx_rate / 16 / samples);

	close(fd);


	// Calculate statistics

	ptr = (unsigned short*) (tr.rx_buf + 2);

	for(int i = 0; i < samples; i++)
		for(int j = 0; j < strlen(channels); j++) {
			int ch = (channels[j] - 0x30) % 16;
			uint16_t val = __bswap_16(*ptr++);

			if(i == 0) { // initial data
				min[ch] = 0xffff;	// max possible value
				max[ch] = 0;		// min possible value
				sum[ch] = 0;		// zero
			}


			if(val < min[ch])
				min[ch] = val; 

			if(val > max[ch])
				max[ch] = val; 

			sum[ch] += val;
		}
		

	for(int j = 0; j < strlen(channels); j++) {
		int ch = (channels[j] - 0x30) % 16;
		printf("Statistics ch[%d]: (min, avg, max, dmin, dmax) = (%d, %lld, %d, %lld, %lld)\n",
			ch, min[ch], sum[ch] / samples,
			max[ch], sum[ch] / samples - min[ch],
			max[ch] - sum[ch] / samples);
	}


	// Write captured data to file

	if (output_file) {
		fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
			pabort("could not open output file");

		ret = write(fd, (void*)tr.rx_buf+2, len-2);
		if (ret != len-2)
			pabort("Not all bytes written to output file");
		else
			printf("Data written to file: %s\n", output_file);

		close(fd);
	}

	if(tr.tx_buf)
		free((void*)tr.tx_buf);

	if(tr.rx_buf)
		free((void*)tr.rx_buf);

	return ret;
}
