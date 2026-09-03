#pragma once
#include "IdtMain.h"

extern bool main_open;
extern bool FirstDraw;
extern bool IdtHotkey;

class StrokeImageClass
{
public:
	StrokeImageClass()
	{
		canvas = nullptr;
		endMode = 0;
		alpha = 255;
		dirty = false;
		SetRectEmpty(&dirtyRect);
		SetRectEmpty(&prevDirtyRect);
		originX = originY = 0;
	}
	~StrokeImageClass()
	{
		if (canvas != nullptr)
		{
			delete canvas;
			canvas = nullptr;
		}
	}

public:
	shared_mutex sm;
	IMAGE* canvas;
	int endMode; // 1 绘制到画布上 2 不绘制到画布上
	int alpha;

	bool dirty;		// 脏矩形优化：画布内容是否有更新
	RECT dirtyRect;	// 脏矩形优化：内容区域（屏幕像素坐标）
	RECT prevDirtyRect;	// 脏矩形优化：已合成内容区域的并集（用于移除/收缩时重绘）
	int originX, originY;	// 区域画布：画布左上角在屏幕中的坐标
};
extern StrokeImageClass strokeImage;

extern shared_mutex StrokeImageListSm;
extern vector<StrokeImageClass*> StrokeImageList;

extern shared_mutex StrokeBackImageSm;

extern bool drawWaiting;
extern shared_mutex drawWaitingSm;

extern IMAGE drawpad;
extern IMAGE window_background;

extern HHOOK DrawpadHookCall;
LRESULT CALLBACK DrawpadHookCallback(int nCode, WPARAM wParam, LPARAM lParam);
void DrawpadInstallHook();

void ResetPrepareCanvas();
int drawpad_main();