#ifndef DOG_IMAGE_H
#define DOG_IMAGE_H

#include <pgmspace.h>
#include <stdint.h>  // 加入這行來定義 uint16_t

// 定義圖片尺寸
#define DOG_WIDTH   231  // 修正為實際寬度
#define DOG_HEIGHT  135

// 宣告圖片陣列
extern const uint16_t dog[] PROGMEM;

#endif