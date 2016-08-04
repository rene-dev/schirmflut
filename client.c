#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32 // Windows
#include <windows.h>
//#include <winsock2.h>
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")

typedef struct
{
	// screen
	int w, h;
	
	// capture
	uint8_t *data;
	int bpp, bpl;
	
	// private
	HDC hScreenDC;
	HDC hMemoryDC;
	HBITMAP hBitmap;
	HBITMAP hOldBitmap;
} capture_ctx;

void capture_init(capture_ctx *ctx)
{
	ctx->hScreenDC = CreateDC("DISPLAY", NULL, NULL, NULL);
	ctx->hMemoryDC = CreateCompatibleDC(ctx->hScreenDC);
	ctx->w = GetDeviceCaps(ctx->hScreenDC, HORZRES);
	ctx->h = GetDeviceCaps(ctx->hScreenDC, VERTRES);
	ctx->hBitmap = CreateCompatibleBitmap(ctx->hScreenDC, ctx->w, ctx->h);
	ctx->hOldBitmap = SelectObject(ctx->hMemoryDC, ctx->hBitmap);
}

void capture_begin(capture_ctx *ctx)
{
	BitBlt(ctx->hMemoryDC, 0, 0, ctx->w, ctx->h, ctx->hScreenDC, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bmpInfoHeader = { sizeof(BITMAPINFOHEADER) };
    bmpInfoHeader.biWidth = ctx->w;
    bmpInfoHeader.biHeight = -ctx->h;
    bmpInfoHeader.biPlanes = 1;
    bmpInfoHeader.biBitCount = 32;

	if (!ctx->data)
	{
		DWORD size = ((ctx->w * bmpInfoHeader.biBitCount + 31) / 32) * 4 * ctx->h;
		ctx->data = malloc(size);
	}

	if (!GetDIBits(ctx->hScreenDC, ctx->hBitmap, 0, ctx->h, ctx->data, (BITMAPINFO*)&bmpInfoHeader, DIB_RGB_COLORS))
	{
		perror("could not read bitmap");
	}
	ctx->bpp = bmpInfoHeader.biBitCount / 8;
	ctx->bpl = ((ctx->w * bmpInfoHeader.biBitCount + 31) / 32) * 4;
}

void capture_end(capture_ctx *ctx)
{
	
}

void capture_close(capture_ctx *ctx)
{
	ctx->hBitmap = SelectObject(ctx->hMemoryDC, ctx->hOldBitmap);
	DeleteDC(ctx->hMemoryDC);
	DeleteDC(ctx->hScreenDC);
}

#else // X11
//#define _DEFAULT_SOURCE 1
//#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

typedef struct
{
	// screen
	int w, h;
	
	// capture
	uint8_t *data;
	int bpp, bpl;
	
	// private
	Display *display;
	Window window;
	XImage *pic;
} capture_ctx;

void capture_init(capture_ctx *ctx)
{
	ctx->display = XOpenDisplay(NULL);
	if (!ctx->display)
	{
		perror("ERROR opening display");
		exit(-1);
	}
	ctx->window = DefaultRootWindow(ctx->display);
	int num_sizes;
	XRRScreenSize *xrrs = XRRSizes(ctx->display, 0, &num_sizes);
	XRRScreenConfiguration *conf = XRRGetScreenInfo(ctx->display, ctx->window);
	Rotation original_rotation;
	SizeID original_size_id = XRRConfigCurrentConfiguration(conf, &original_rotation);
	ctx->w = xrrs[original_size_id].width;
	ctx->h = xrrs[original_size_id].height;
}

void capture_begin(capture_ctx *ctx)
{
	ctx->pic = XGetImage(ctx->display, ctx->window, 0, 0, ctx->w, ctx->h, AllPlanes, ZPixmap);
	ctx->data = ctx->pic->data;
	ctx->bpp = ctx->pic->bits_per_pixel / 8;
	ctx->bpl = ctx->pic->bytes_per_line;
}

void capture_end(capture_ctx *ctx)
{
	ctx->pic->f.destroy_image(ctx->pic);
	ctx->pic = NULL;
}

void capture_close(capture_ctx *ctx)
{
	
}

#endif

// tx buffer size
#define BUFFER_SIZE 37000 //  1536 //65000 //  1536

int main(int argc, char *argv[])
{
	// parameter stuff
	if (argc < 5)
	{
		fprintf(stderr, "usage %s hostname port output_width output_height\n", argv[0]);
		exit(0);
	}
	char *host = argv[1];
	int port = atoi(argv[2]);
	int out_w = atoi(argv[3]);
	int out_h = atoi(argv[4]);
	
	// screen stuff
	capture_ctx capture;
	capture_init(&capture);
	printf("Capture resolution: %d %d\n", capture.w, capture.h);
	uint8_t *out_data = malloc(out_w * out_h * 3 + BUFFER_SIZE);
	float dx = capture.w / (float)out_w, dy = capture.h / (float)out_h;

	// network stuff
	#ifdef _WIN32
	WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        perror("WSAStartup failed");
        exit(-1);
    }
	#else
	#define SOCKET int
	#endif
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
	serv_addr.sin_port = htons((uint16_t)port);
	SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0)
	{
		perror("ERROR opening socket");
		exit(-1);
	}

	// protocol stuff
	uint8_t buffer[BUFFER_SIZE];
	uint16_t *header = (uint16_t*)buffer;
	header[0] = 0;
	header[2] = (uint16_t)out_w;
	int lines_per_packet = (BUFFER_SIZE - 6) / (out_w * 3);
	if (out_h < lines_per_packet)
		lines_per_packet = out_h;
	
	printf("Lines per packet: %d\n", lines_per_packet);
	printf("Packet size: %d\n", 6 + lines_per_packet * out_w * 3);

	while (42)
	{
		capture_begin(&capture);

		//clock_t start_scale = clock();
		uint8_t *out = out_data;
		float fx = 0.0f, fy = 0.0f;
		int ix = 0, iy = 0;
		for (int y = 0; y < out_h; y++)
		{
			float nfy = 1.0f - fy;

			uint8_t *tl = (unsigned char*)capture.data + iy * capture.bpl;
			uint8_t *tr = tl + capture.bpp;
			uint8_t *bl = tl + capture.bpl;
			uint8_t *br = tr + capture.bpl;
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
				int inc_x = (int)fx, step = inc_x * capture.bpp;
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
		capture_end(&capture);
		//printf("Scale: %3dms\n", (clock() - start_scale) * 1000 / CLOCKS_PER_SEC);

		//clock_t start_send = clock();
		for (int y = 0; y < out_h; y += lines_per_packet)
		{
			header[1] = (uint16_t)y;
			memcpy(buffer + 6, out_data + 3 * y * out_w, 3 * lines_per_packet * out_w);
			sendto(sockfd, (const char*)buffer, 6 + lines_per_packet * out_w * 3, 0, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
			usleep(12000);
		}
		//printf("Send:  %3dms\n\n", (clock() - start_send) * 1000 / CLOCKS_PER_SEC);
	}
	free(out_data);
	capture_close(&capture);
	return 0;
}
