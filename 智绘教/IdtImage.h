#pragma once
#include "IdtMain.h"

//drawpad画笔
extern IMAGE alpha_drawpad; //临时画板
extern IMAGE putout; //主画板上叠加的控件内容
extern IMAGE tester; //图形绘制画板
extern IMAGE pptdrawpad; //PPT控件画板

extern int recall_image_recond, recall_image_reference;
extern shared_mutex RecallImageManipulatedSm;
extern chrono::high_resolution_clock::time_point RecallImageManipulated;
extern tm RecallImageTm;

unsigned long long NextRecallToken();

struct RecallStruct
{
	IMAGE img;
	std::map<std::pair<int, int>, bool> extreme_point;
	int type;
	pair<int, int> recond;
	vector<BYTE> png;			// 压缩存储（非空时 img 已释放）
	unsigned long long token;	// 压缩任务匹配标识

	RecallStruct() : type(0), recond({ 0, 0 }), token(NextRecallToken()) {}
	RecallStruct(IMAGE i, std::map<std::pair<int, int>, bool> e, int t, pair<int, int> r = { 0, 0 })
		: img(std::move(i)), extreme_point(std::move(e)), type(t), recond(r), token(NextRecallToken()) {}
};
extern int RecallImagePeak;
extern deque<RecallStruct> RecallImage;//撤回栈
extern shared_mutex RecallImageSm;		// 撤销栈压缩/读取同步锁

// 撤销栈图像压缩（PNG 内存编码，稀疏墨迹压缩率极高）
bool EncodeImageToPngMemory(IMAGE* img, vector<BYTE>& outPng);
bool DecodePngMemoryToImage(const vector<BYTE>& png, IMAGE& outImg);
IMAGE GetRecallEntryImage(RecallStruct& entry);	// 按需解码并返回完整图像副本
void MaybeCompressRecallEntries();				// 空闲时异步压缩撤销历史（单线程化）

//悬浮窗
extern IMAGE background;
extern Graphics graphics;

Bitmap* IMAGEToBitmap(IMAGE* easyXImage);
bool ImgCpy(IMAGE* tag, IMAGE* src);

extern shared_mutex loadImageSm;
void idtLoadImage(IMAGE* pDstImg, LPCTSTR pImgFile, int nWidth = 0, int nHeight = 0, bool bResize = false);
void idtLoadImage(IMAGE* pDstImg, LPCTSTR pResType, LPCTSTR pResName, int nWidth = 0, int nHeight = 0, bool bResize = false);