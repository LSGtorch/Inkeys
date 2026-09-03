#include "IdtImage.h"

//drawpad画笔
IMAGE alpha_drawpad; //临时画板（按需分配）
IMAGE putout; //主画板上叠加的控件内容
IMAGE tester; //图形绘制画板（按需分配）
IMAGE pptdrawpad; //PPT控件画板（按需分配）

int recall_image_recond, recall_image_reference;
shared_mutex RecallImageManipulatedSm;
chrono::high_resolution_clock::time_point RecallImageManipulated;

tm RecallImageTm;
int RecallImagePeak = 0;
deque<RecallStruct> RecallImage;//撤回栈

//悬浮窗
IMAGE background(576, 386);
Graphics graphics(GetImageHDC(&background));

Bitmap* IMAGEToBitmap(IMAGE* easyXImage)
{
	if (!easyXImage || easyXImage->getwidth() <= 0 || easyXImage->getheight() <= 0) {
		return nullptr;
	}

	// 获取 EasyX 图像的信息
	int width = easyXImage->getwidth();
	int height = easyXImage->getheight();
	int channels = 4;  // 假设 EasyX 图像使用 32 位 ARGB 格式

	// 创建 GDI+ Bitmap
	Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
	if (!bitmap) {
		return nullptr;
	}

	// 锁定 GDI+ Bitmap 的数据
	Gdiplus::BitmapData bitmapData;
	Gdiplus::Rect rect(0, 0, width, height);
	bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bitmapData);

	// 将 EasyX 图像数据复制到 GDI+ Bitmap
	DWORD* srcData = GetImageBuffer(easyXImage);
	BYTE* destData = static_cast<BYTE*>(bitmapData.Scan0);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			// 每个像素有四个字节，分别是 B, G, R, A
			destData[4 * (y * width + x) + 0] = GetRValue(srcData[(y * width + x)]);  // Blue
			destData[4 * (y * width + x) + 1] = GetGValue(srcData[(y * width + x)]);  // Green
			destData[4 * (y * width + x) + 2] = GetBValue(srcData[(y * width + x)]);  // Red
			destData[4 * (y * width + x) + 3] = (srcData[(y * width + x)] >> 24) & 0xFF;  // Alpha
		}
	}

	// 解锁 GDI+ Bitmap 的数据
	bitmap->UnlockBits(&bitmapData);

	return bitmap;
}
bool ImgCpy(IMAGE* tag, IMAGE* src)
{
	if (tag == NULL || src == NULL) return false;
	if (tag->getwidth() != src->getwidth() || tag->getheight() != src->getheight())
	{
		tag->Resize(src->getwidth(), src->getheight());
	}

	int width = src->getwidth();
	int height = src->getheight();
	DWORD* pSrc = GetImageBuffer(src);
	DWORD* pTag = GetImageBuffer(tag);

	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			pTag[y * width + x] = pSrc[y * width + x];
		}
	}

	return true;
}

shared_mutex loadImageSm;
void idtLoadImage(IMAGE* pDstImg, LPCTSTR pImgFile, int nWidth, int nHeight, bool bResize)
{
	lock_guard loadImageLock(loadImageSm);
	loadimage(pDstImg, pImgFile, nWidth, nHeight, bResize);
}
void idtLoadImage(IMAGE* pDstImg, LPCTSTR pResType, LPCTSTR pResName, int nWidth, int nHeight, bool bResize)
{
	lock_guard loadImageLock(loadImageSm);
	loadimage(pDstImg, pResType, pResName, nWidth, nHeight, bResize);
}
// -------------------------
// 撤销栈图像压缩（内存优化）

#define STB_IMAGE_IMPLEMENTATION
#include "stbimage/stb_image.h"
#include "stbimage/stb_image_write.h" // 仅使用声明，实现位于 IdtDraw.cpp

shared_mutex RecallImageSm;
static atomic<unsigned long long> RecallTokenCounter = 0;
static atomic<bool> recallCompressing = false;

unsigned long long NextRecallToken()
{
	return ++RecallTokenCounter;
}

// EasyX IMAGE（预乘 BGRA）→ 非预乘 RGBA 内存 PNG
bool EncodeImageToPngMemory(IMAGE* img, vector<BYTE>& outPng)
{
	if (img == nullptr) return false;

	int width = img->getwidth(), height = img->getheight();
	if (width <= 0 || height <= 0) return false;

	DWORD* pMem = GetImageBuffer(img);
	if (pMem == nullptr) return false;

	vector<unsigned char> data((size_t)width * height * 4);
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			DWORD color = pMem[(size_t)y * width + x];
			unsigned char a = (color >> 24) & 0xFF;
			size_t o = ((size_t)y * width + x) * 4;

			if (a != 0)
			{
				data[o + 0] = (unsigned char)((((color >> 16) & 0xFF) * 255) / a);
				data[o + 1] = (unsigned char)((((color >> 8) & 0xFF) * 255) / a);
				data[o + 2] = (unsigned char)(((color & 0xFF) * 255) / a);
			}
			else
			{
				data[o + 0] = data[o + 1] = data[o + 2] = 0;
			}
			data[o + 3] = a;
		}
	}

	auto writeCallback = [](void* context, void* chunk, int size)
		{
			vector<BYTE>* out = static_cast<vector<BYTE>*>(context);
			BYTE* bytes = static_cast<BYTE*>(chunk);
			out->insert(out->end(), bytes, bytes + size);
		};

	stbi_write_png_compression_level = 1; // 低压缩级别保证编码速度
	int result = stbi_write_png_to_func(writeCallback, &outPng, width, height, 4, data.data(), width * 4);

	return result != 0 && !outPng.empty();
}
// 内存 PNG → EasyX IMAGE（恢复预乘 BGRA）
bool DecodePngMemoryToImage(const vector<BYTE>& png, IMAGE& outImg)
{
	if (png.empty()) return false;

	int width = 0, height = 0, channels = 0;
	stbi_uc* data = stbi_load_from_memory(png.data(), (int)png.size(), &width, &height, &channels, 4);
	if (data == nullptr || width <= 0 || height <= 0) return false;

	outImg.Resize(width, height);
	DWORD* pMem = GetImageBuffer(&outImg);
	if (pMem == nullptr)
	{
		stbi_image_free(data);
		return false;
	}

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			size_t o = ((size_t)y * width + x) * 4;
			unsigned char r = data[o + 0], g = data[o + 1], b = data[o + 2], a = data[o + 3];

			pMem[(size_t)y * width + x] = ((DWORD)a << 24) | ((DWORD)(r * a / 255) << 16) | ((DWORD)(g * a / 255) << 8) | (DWORD)(b * a / 255);
		}
	}

	stbi_image_free(data);
	return true;
}

IMAGE GetRecallEntryImage(RecallStruct& entry)
{
	shared_lock lock(RecallImageSm);
	if (!entry.png.empty())
	{
		IMAGE decoded;
		if (DecodePngMemoryToImage(entry.png, decoded)) return decoded;
	}
	return entry.img;
}
void MaybeCompressRecallEntries()
{
	if (recallCompressing) return;

	unsigned long long targetToken = 0;
	IMAGE snapshot;
	{
		shared_lock lock(RecallImageSm);
		for (auto& entry : RecallImage)
		{
			if (entry.png.empty() && entry.img.getwidth() > 0 && entry.img.getheight() > 0)
			{
				targetToken = entry.token;
				snapshot = entry.img; // 拷贝像素后交由后台线程编码
				break;
			}
		}
	}
	if (targetToken == 0) return;

	recallCompressing = true;
	thread([targetToken, snapshot]() mutable
		{
			vector<BYTE> png;
			EncodeImageToPngMemory(&snapshot, png);

			unique_lock lock(RecallImageSm);
			for (auto& entry : RecallImage)
			{
				if (entry.token == targetToken && entry.png.empty() && !png.empty())
				{
					entry.png = std::move(png);
					entry.img = IMAGE(); // 释放全屏位图
					break;
				}
			}
			lock.unlock();

			recallCompressing = false;
		}).detach();
}
