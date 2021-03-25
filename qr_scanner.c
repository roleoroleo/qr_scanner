/*
 * Copyright (c) 2021 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Read the last h264 i-frame from the buffer, convert it using libavcodec
 * and process with quirc.
 * The position of the frame is written in /tmp/iframe.idx
 */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <getopt.h>
#include <time.h>

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include "libavcodec/avcodec.h"
#include "quirc.h"

#define BUF_OFFSET 300
#define BUF_SIZE 1786156

#define BUFFER_FILE "/dev/shm/fshare_frame_buf"
#define I_FILE "/tmp/iframe.idx"
#define FF_INPUT_BUFFER_PADDING_SIZE 32

#define RESOLUTION_HIGH 0
#define RESOLUTION_LOW 1

#define W_LOW 640
#define H_LOW 360
#define W_HIGH 1920
#define H_HIGH 1080

#define SCAN_INTERVAL_MS 50
#define LED_CHANGE 1 // Change led on detect QRCODE

typedef struct {
    int sps_addr;
    int sps_len;
    int pps_addr;
    int pps_len;
    int idr_addr;
    int idr_len;
} frame;

int debug;

unsigned char *addr;

void *cb_memcpy(void * dest, const void * src, size_t n)
{
    unsigned char *uc_src = (unsigned char *) src;
    unsigned char *uc_dest = (unsigned char *) dest;

    if (uc_src + n > addr + BUF_SIZE) {
        memcpy(uc_dest, uc_src, addr + BUF_SIZE - uc_src);
        memcpy(uc_dest + (addr + BUF_SIZE - uc_src), addr + BUF_OFFSET, n - (addr + BUF_SIZE - uc_src));
    } else {
        memcpy(dest, src, n);
    }
    return dest;
}

// Convert h264 frame to gray scale bitmap image
int frame_decode(unsigned char *outbuffer, unsigned char *p, int length)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    AVFrame *picture;
    int got_picture, len;
    FILE *fOut;
    uint8_t *inbuf;
    AVPacket avpkt;
    int i, j, size;

//////////////////////////////////////////////////////////
//                    Reading H264                      //
//////////////////////////////////////////////////////////

    if (debug) fprintf(stderr, "Starting decode\n");

    av_init_packet(&avpkt);

    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        if (debug) fprintf(stderr, "Codec h264 not found\n");
        return -2;
    }

    c = avcodec_alloc_context3(codec);
    picture = av_frame_alloc();

    if((codec->capabilities) & AV_CODEC_CAP_TRUNCATED)
        (c->flags) |= AV_CODEC_FLAG_TRUNCATED;

    if (avcodec_open2(c, codec, NULL) < 0) {
        if (debug) fprintf(stderr, "Could not open codec h264\n");
        av_free(c);
        return -2;
    }

    inbuf = (uint8_t *) malloc(length + FF_INPUT_BUFFER_PADDING_SIZE);
    if (inbuf == NULL) {
        if (debug) fprintf(stderr, "Error allocating memory\n");
        avcodec_close(c);
        av_free(c);
        return -2;
    }
    memset(inbuf + length, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    // Get only 1 frame
    memcpy(inbuf, p, length);
    avpkt.size = length;
    avpkt.data = inbuf;

    // Decode frame
    if (debug) fprintf(stderr, "Decode frame\n");
    if (c->codec_type == AVMEDIA_TYPE_VIDEO ||
         c->codec_type == AVMEDIA_TYPE_AUDIO) {

        len = avcodec_send_packet(c, &avpkt);
        if (len < 0 && len != AVERROR(EAGAIN) && len != AVERROR_EOF) {
            if (debug) fprintf(stderr, "Error decoding frame\n");
            return -2;
        } else {
            if (len >= 0)
                avpkt.size = 0;
            len = avcodec_receive_frame(c, picture);
            if (len >= 0)
                got_picture = 1;
        }
    }
    if(!got_picture) {
        if (debug) fprintf(stderr, "No input frame\n");
        free(inbuf);
        av_frame_free(&picture);
        avcodec_close(c);
        av_free(c);
        return -2;
    }

    if (debug) fprintf(stderr, "Writing yuv buffer\n");
    memset(outbuffer, 0x80, c->width * c->height);
    memcpy(outbuffer, picture->data[0], c->width * c->height);

    // Clean memory
    if (debug) fprintf(stderr, "Cleaning ffmpeg memory\n");
    free(inbuf);
    av_frame_free(&picture);
    avcodec_close(c);
    av_free(c);

    return 0;
}

void usage(char *prog_name)
{
    fprintf(stderr, "Usage: %s [options]\n", prog_name);
    fprintf(stderr, "\t-r, --res RES           Set resolution: \"low\" or \"high\" (default \"high\")\n");
    fprintf(stderr, "\t-h, --help              Show this help\n");
}

void printTime()
{
	char tbuffer[26];
	time_t timer;
	struct tm* tm_info;

	timer = time(NULL);
	tm_info = localtime(&timer);
	strftime(tbuffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
	fprintf(stderr, "%s", tbuffer);
}

int main(int argc, char **argv)
{
    FILE *fIdx, *fBuf;
    uint32_t offset, length;
    frame hl_frame[2];
    unsigned char *bufferh264, *bufferyuv;

    struct quirc *qr;
    uint8_t *buf;

    int res = RESOLUTION_LOW;
    int width =  (res == RESOLUTION_HIGH ? W_HIGH : W_LOW);
    int height = (res == RESOLUTION_HIGH ? H_HIGH : H_LOW);;
    int w, h;
    int i, count;

    int c;
    int err;


    debug = 0;

    while (1) {
        static struct option long_options[] = {
            {"res",       required_argument, 0, 'r'},
            {"help",      no_argument,       0, 'h'},
            {0,           0,                 0,  0 }
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "r:wh",
            long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'r':
                if (strcasecmp("low", optarg) == 0) {
                    res = RESOLUTION_LOW;
                    width = W_LOW;
                    height = H_LOW;
                } else {
                    res = RESOLUTION_HIGH;
                    width = W_HIGH;
                    height = H_HIGH;
                }
                break;

            case 'h':
            default:
                usage(argv[0]);
                exit(-1);
                break;
        }
    }

    if (debug) fprintf(stderr, "Starting program\n");

    qr = quirc_new();
    if (!qr) {
        perror("couldn't allocate QR decoder");
        return -1;
    }

    if (quirc_resize(qr, width, height) < 0) {
        perror("couldn't allocate QR buffer");
        quirc_destroy(qr);
        return -2;
    }

    for (;;) {
        fIdx = fopen(I_FILE, "r");
        if ( fIdx == NULL ) {
            fprintf(stderr, "Could not open file %s\n", I_FILE);
            exit(-1);
        }
        if (fread(hl_frame, 1, 2 * sizeof(frame), fIdx) != 2 * sizeof(frame)) {
            fprintf(stderr, "Error reading file %s\n", I_FILE);
            exit(-1);
        }

        fBuf = fopen(BUFFER_FILE, "r") ;
        if (fBuf == NULL) {
            fprintf(stderr, "Could not open file %s\n", BUFFER_FILE);
            exit(-1);
        }

        // Map file to memory
        addr = (unsigned char*) mmap(NULL, BUF_SIZE, PROT_READ, MAP_SHARED, fileno(fBuf), 0);
        if (addr == MAP_FAILED) {
            fprintf(stderr, "Error mapping file %s\n", BUFFER_FILE);
            exit(-1);
        }
        if (debug) fprintf(stderr, "Mapping file %s, size %d, to %08x\n", BUFFER_FILE, BUF_SIZE, addr);

        // Closing the file
        fclose(fBuf) ;

        bufferh264 = (unsigned char *) malloc(hl_frame[res].sps_len + hl_frame[res].pps_len + hl_frame[res].idr_len);
        if (bufferh264 == NULL) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit -1;
        }

        cb_memcpy(bufferh264, addr + hl_frame[res].sps_addr, hl_frame[res].sps_len);
        cb_memcpy(bufferh264 + hl_frame[res].sps_len, addr + hl_frame[res].pps_addr, hl_frame[res].pps_len);
        cb_memcpy(bufferh264 + hl_frame[res].sps_len + hl_frame[res].pps_len, addr + hl_frame[res].idr_addr, hl_frame[res].idr_len);

        // Quirc begin
        buf = quirc_begin(qr, &w, &h);

        // Convert h264 frame to gray scale bitmap image
        if (debug) fprintf(stderr, "Decoding h264 frame\n");
        if(frame_decode(buf, bufferh264, hl_frame[res].sps_len + hl_frame[res].pps_len + hl_frame[res].idr_len) < 0) {
            fprintf(stderr, "Error decoding h264 frame\n");
            exit(-2);
        }
        free(bufferh264);

        // Quirc end
        quirc_end(qr);

        count = quirc_count(qr);

         fprintf(stderr, "Decoding qrcode , count: %d (res: %d) \n", count, res);

        if(count <= 0 && LED_CHANGE) system("/home/yi-hack/bin/ipc_cmd -l OFF");

        for (i = 0; i < count; i++) {
            struct quirc_code code;
            struct quirc_data data;

            quirc_extract(qr, i, &code);
            err = quirc_decode(&code, &data);
            if (err)
                printf("Decode failed: %s\n", quirc_strerror(err));
            else{

				printTime();
				fprintf(stderr, " - Data: %s\n", data.payload);
				fprintf(stdout, "\a" ); // BEEP !!!

				if(LED_CHANGE) system("/home/yi-hack/bin/ipc_cmd -l ON");
            }
        }

        // Unmap file from memory
        if (munmap(addr, BUF_SIZE) == -1) {
            fprintf(stderr, "Error munmapping file\n");
        } else {
            if (debug) fprintf(stderr, "Unmapping file %s, size %d, from %08x\n", BUFFER_FILE, BUF_SIZE, addr);
        }

        usleep(SCAN_INTERVAL_MS * 1000); //ms
    }

    quirc_destroy(qr);

    return 0;
}
