#include "TextRasterizer.h"
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace renderer {
std::vector<uint8_t> rasterizeText(const std::wstring& text,int& width,int& height) {
    static_assert(sizeof(wchar_t)==4);
    auto string=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(text.data()),text.size()*4,kCFStringEncodingUTF32LE,false);
    if(!string) throw std::runtime_error("Invalid label text");
    auto font=CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem,24,nullptr);
    const void* keys[]={kCTFontAttributeName,kCTForegroundColorFromContextAttributeName};
    const void* values[]={font,kCFBooleanTrue};
    auto attributes=CFDictionaryCreate(nullptr,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    auto attributed=CFAttributedStringCreate(nullptr,string,attributes);
    auto line=CTLineCreateWithAttributedString(attributed);
    CGFloat ascent=0,descent=0,leading=0;
    width=std::max(1,int(std::ceil(CTLineGetTypographicBounds(line,&ascent,&descent,&leading)))+4);
    height=std::max(1,int(std::ceil(ascent+descent+leading))+4);
    std::vector<uint8_t> rgba(size_t(width)*height*4);
    auto space=CGColorSpaceCreateDeviceRGB();
    auto context=CGBitmapContextCreate(rgba.data(),width,height,8,width*4,space,CGBitmapInfo(kCGImageAlphaPremultipliedLast)|kCGBitmapByteOrder32Big);
    if(context) {
        CGContextSetRGBFillColor(context,1,1,1,1);
        CGContextSetTextPosition(context,2,std::ceil(descent)+2);CTLineDraw(line,context);CGContextRelease(context);
    }
    CGColorSpaceRelease(space);CFRelease(line);CFRelease(attributed);CFRelease(attributes);CFRelease(font);CFRelease(string);
    if(!context) throw std::runtime_error("Unable to rasterize label");
    for(size_t p=0;p<rgba.size();p+=4) rgba[p]=rgba[p+1]=rgba[p+2]=255;
    return rgba;
}
}
