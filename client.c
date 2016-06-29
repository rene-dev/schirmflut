//#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
//#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <time.h>

int main(int argc, char *argv[])
{
	// parameter stuff
	if (argc < 5)
	{
		fprintf(stderr,"usage %s hostname port output_width output_height\n", argv[0]);
		exit(0);
	}
	char *host = argv[1];
	int port = atoi(argv[2]);
	int out_w = atoi(argv[3]);
	int out_h = atoi(argv[4]);
	
	// X stuff
	Display *display = XOpenDisplay(NULL);
	if (!display)
	{
		perror("ERROR opening display");
		exit(-1);
	}
	Window window = DefaultRootWindow(display);
	int num_sizes;
	XRRScreenSize *xrrs = XRRSizes(display, 0, &num_sizes);
	XRRScreenConfiguration *conf = XRRGetScreenInfo(display, window);
	Rotation original_rotation;
	SizeID original_size_id = XRRConfigCurrentConfiguration(conf, &original_rotation);
	int in_w = xrrs[original_size_id].width;
	int in_h = xrrs[original_size_id].height;
	uint8_t *out_data = malloc(out_w * out_h * 3);
	float dx = in_w / (float)out_w, dy = in_h / (float)out_h;

	// network stuff
	struct hostent *server;
	server = gethostbyname(host);
	if (server == NULL)
	{
		perror("ERROR no such host");
		exit(-1);
	}
	struct sockaddr_in serv_addr = {0};
	serv_addr.sin_family = AF_INET;
	memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
	serv_addr.sin_port = htons(port);
	int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0)
	{
		perror("ERROR opening socket");
		exit(-1);
	}

	// protocol stuff
	uint8_t buffer[65000];
	uint16_t *header = (uint16_t*)buffer;
	header[0] = 0;
	header[2] = out_w;
	int lines_per_packet = (65000 - 6) / (out_w * 3);

	while (42)
	{
		XImage *pic = XGetImage(display, window, 0, 0, in_w, in_h, AllPlanes, ZPixmap);

		//clock_t start_scale = clock();
		int bpp = pic->bits_per_pixel / 8;

		uint8_t *out = out_data;
		float fx = 0.0f, fy = 0.0f;
		int ix = 0, iy = 0;
		float dx = in_w / (float)out_w, dy = in_h / (float)out_h;
	
		float ftl = 1.0f, ftr = 0.0f, fbl = 0.0f, fbr = 0.0f;
	
		for (int y = 0; y < out_h; y++)
		{
			float nfy = 1.0f - fy;

			uint8_t *tl = (unsigned char*)pic->data + iy * pic->bytes_per_line;
			uint8_t *tr = tl + bpp;
			uint8_t *bl = tl + pic->bytes_per_line;
			uint8_t *br = tr + pic->bytes_per_line;
			fx = 0.0f;
			ix = 0;
			for (int x = 0; x < out_w; x++)
			{
			#if 1 // bilinear interpolated scaling
				float nfx = 1.0f - fx;

				float ftl = nfx * nfy;
				float ftr =  fx * nfy;
				float fbl = nfx *  fy;
				float fbr =  fx *  fy;

				out[2] = (uint8_t)(tl[0] * ftl + tr[0] * ftr + bl[0] * fbl + br[0] * fbr);
				out[1] = (uint8_t)(tl[1] * ftl + tr[1] * ftr + bl[1] * fbl + br[1] * fbr);
				out[0] = (uint8_t)(tl[2] * ftl + tr[2] * ftr + bl[2] * fbl + br[2] * fbr);
			#else // averaged scaling (faster, good enough?)
				out[2] = (tl[0] + tr[0] + bl[0] + br[0]) >> 2;
				out[1] = (tl[1] + tr[1] + bl[1] + br[1]) >> 2;
				out[0] = (tl[2] + tr[2] + bl[2] + br[2]) >> 2;
			#endif
				out += 3;
				fx += dx;
				int inc_x = (int)fx, step = inc_x * bpp;
				fx -= inc_x;
				ix += inc_x;
				tl += step; tr += step;
				bl += step; br += step;
			}
			fy += dy;
			int inc_y = (int)fy;
			fy -= inc_y;
			iy += inc_y;
		}
		pic->f.destroy_image(pic);
		//printf("Scale: %3dms\n", (clock() - start_scale) * 1000 / CLOCKS_PER_SEC);

		//clock_t start_send = clock();
		for (int y = 0; y < out_h; y += lines_per_packet)
		{
			header[1] = y;
			memcpy(buffer + 6, out_data + 3 * y * out_w, 3 * lines_per_packet * out_w);
			sendto(sockfd, buffer, 6 + lines_per_packet * out_w * 3, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
		}
		//printf("Send:  %3dms\n\n", (clock() - start_send) * 1000 / CLOCKS_PER_SEC);
	}
	free(out_data);
	return 0;
}
