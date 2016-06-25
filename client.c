#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

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
		int bpp = pic->bits_per_pixel / 8;
		for (int y = 0; y < out_h; y += lines_per_packet)
		{
			header[1] = y;
			for (int ly = 0; ly < lines_per_packet && y + ly < out_h; ly++)
			{
				uint8_t *in = (unsigned char*)pic->data + ((y + ly) * in_h / out_h) * pic->bytes_per_line;
				uint8_t *out = buffer + 6 + 3 * ly * out_w;
				
				// append a line of bilinear interpolated pixel values to output buffer
				float fy = (y + ly) * in_h / (float)out_h;
				int iy = (int)fy;
				fy -= iy;
				float nfy = nfy = 1.0f - fy;
				for (int x = 0; x < out_w; x++)
				{
					float fx = x * in_w / (float)out_w;
					int ix = (int)fx;
					fx -= ix;
					float nfx = 1.0f - fx;
					
					uint8_t *tl = in + ix * bpp;
					uint8_t *tr = tl + bpp;
					uint8_t *bl = tl + pic->bytes_per_line;
					uint8_t *br = tr + pic->bytes_per_line;
					
					float t[3] = {
						tl[2] * nfx + tr[2] * fx,
						tl[1] * nfx + tr[1] * fx,
						tl[0] * nfx + tr[0] * fx };
					float b[3] = {
						bl[2] * nfx + br[2] * fx,
						bl[1] * nfx + br[1] * fx,
						bl[0] * nfx + br[0] * fx };
					float result[3] = {
						t[0] * nfy + b[0] * fy,
						t[1] * nfy + b[1] * fy,
						t[2] * nfy + b[2] * fy };
					
					out[0] = (uint8_t)result[0];
					out[1] = (uint8_t)result[1];
					out[2] = (uint8_t)result[2];
					out += 3;
				}
			}
			sendto(sockfd, buffer, 6 + lines_per_packet * out_w * 3, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
			//usleep(15000 / (out_h / lines_per_packet)); // only send fast enough for ~60fps
		}
		pic->f.destroy_image(pic);
	}
	return 0;
}
