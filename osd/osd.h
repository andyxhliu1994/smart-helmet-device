#ifndef _OSD_H_
#define _OSD_H_

#include "common.h"
#include "freetype2/ft2build.h"
#include <wchar.h>

#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include FT_IMAGE_H

#define OSD_FMT_SPACE " "
#define OSD_FMT_TIME0 "24hour"
#define OSD_FMT_TIME1 "12hour"
#define OSD_FMT_WEEK0 "WEEKCN"
#define OSD_FMT_WEEK1 "WEEK"
#define OSD_FMT_CHR   "CHR"
#define OSD_FMT_YMD0 "YYYY-MM-DD"
#define OSD_FMT_YMD1 "MM-DD-YYYY"
#define OSD_FMT_YMD2 "DD-MM-YYYY"
#define OSD_FMT_YMD3 "YYYY/MM/DD"
#define OSD_FMT_YMD4 "MM/DD/YYYY"
#define OSD_FMT_YMD5 "DD/MM/YYYY"
#define MAX_WCH_BYTE 128

enum {
	OSD_TYPE_DATE = 0,
	OSD_TYPE_IMAGE = 1,
	OSD_TYPE_TEXT = 2,
	OSD_TYPE_BORDER = 3,
};

enum BorderEffect {
	BORDER_LINE = 0,
	BORDER_DOTTED,
	BORDER_WATERFULL_LIGHT,
};

typedef struct DrawRect {
	int x;
	int y;
	int w;
	int h;
} DrawRect;

typedef struct BorderInfo {
	DrawRect rect;
	uint32_t color;
	uint32_t color_key;
	int thick;
	int display_style;
	int dotted_offset;
	int interval;
} BorderInfo;


typedef struct text_data {
	wchar_t wch[MAX_WCH_BYTE];
	unsigned int font_size;
	unsigned int font_color;
	unsigned int color_inverse;
	const char *font_path;
	char format[128];
} text_s;

typedef struct border_data {
	int color_index;
	int color_key;
	int thick;
	int display_style;
} border_s;

typedef struct osd_data {
	int type;
	union {
		const char *image;
		text_s text;
		border_s border;
	};
	int width;
	int height;
	unsigned char *buffer;
	unsigned int size;

	int origin_x;
	int origin_y;
	int enable;
} osd_data_s;

typedef struct _border {
	int type;
	int enable;
	int origin_x;
	int origin_y;	
	int width;
	int height;	
	unsigned int color; //弃用
	unsigned char *buffer;
	unsigned int size;
	border_s border;
} border_data_s;


#define OSD_MPP_CHN 0

#define OSD_TIME_COLOR	 (0xfffff799)
#define OSD_BORDER_COLOR (0xff00ffff)

/***************************************************************************** */
int init_osd() ;
int deinit_osd() ;

int init_broder_osd();
int deinit_broder_osd();

#endif