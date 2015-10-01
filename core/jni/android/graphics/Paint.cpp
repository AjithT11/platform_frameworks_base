/* libs/android_runtime/android/graphics/Paint.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "Paint"

#include <utils/Log.h>

#include "jni.h"
#include "GraphicsJNI.h"
#include "core_jni_helpers.h"
#include <ScopedStringChars.h>
#include <ScopedUtfChars.h>

#include "SkBlurDrawLooper.h"
#include "SkColorFilter.h"
#include "SkMaskFilter.h"
#include "SkRasterizer.h"
#include "SkShader.h"
#include "SkTypeface.h"
#include "SkXfermode.h"
#include "unicode/uloc.h"
#include "unicode/ushape.h"
#include "utils/Blur.h"

#include <minikin/GraphemeBreak.h>
#include <minikin/Measurement.h>
#include "MinikinSkia.h"
#include "MinikinUtils.h"
#include "Paint.h"
#include "TypefaceImpl.h"

#include <vector>
#include <memory>

// temporary for debugging
#include <Caches.h>
#include <utils/Log.h>

namespace android {

struct JMetricsID {
    jfieldID    top;
    jfieldID    ascent;
    jfieldID    descent;
    jfieldID    bottom;
    jfieldID    leading;
};

static jclass   gFontMetrics_class;
static JMetricsID gFontMetrics_fieldID;

static jclass   gFontMetricsInt_class;
static JMetricsID gFontMetricsInt_fieldID;

static void defaultSettingsForAndroid(Paint* paint) {
    // GlyphID encoding is required because we are using Harfbuzz shaping
    paint->setTextEncoding(Paint::kGlyphID_TextEncoding);
}

struct LocaleCacheEntry {
    std::string javaLocale;
    std::string languageTag;
};

static thread_local LocaleCacheEntry sSingleEntryLocaleCache;

namespace PaintGlue {
    enum MoveOpt {
        AFTER, AT_OR_AFTER, BEFORE, AT_OR_BEFORE, AT
    };

    static void finalizer(JNIEnv* env, jobject clazz, jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        delete obj;
    }

    static jlong init(JNIEnv* env, jobject) {
        static_assert(1 <<  0 == SkPaint::kAntiAlias_Flag,          "paint_flags_mismatch");
        static_assert(1 <<  2 == SkPaint::kDither_Flag,             "paint_flags_mismatch");
        static_assert(1 <<  3 == SkPaint::kUnderlineText_Flag,      "paint_flags_mismatch");
        static_assert(1 <<  4 == SkPaint::kStrikeThruText_Flag,     "paint_flags_mismatch");
        static_assert(1 <<  5 == SkPaint::kFakeBoldText_Flag,       "paint_flags_mismatch");
        static_assert(1 <<  6 == SkPaint::kLinearText_Flag,         "paint_flags_mismatch");
        static_assert(1 <<  7 == SkPaint::kSubpixelText_Flag,       "paint_flags_mismatch");
        static_assert(1 <<  8 == SkPaint::kDevKernText_Flag,        "paint_flags_mismatch");
        static_assert(1 << 10 == SkPaint::kEmbeddedBitmapText_Flag, "paint_flags_mismatch");

        Paint* obj = new Paint();
        defaultSettingsForAndroid(obj);
        return reinterpret_cast<jlong>(obj);
    }

    static jlong initWithPaint(JNIEnv* env, jobject clazz, jlong paintHandle) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        Paint* obj = new Paint(*paint);
        return reinterpret_cast<jlong>(obj);
    }

    static void reset(JNIEnv* env, jobject clazz, jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        obj->reset();
        defaultSettingsForAndroid(obj);
    }

    static void assign(JNIEnv* env, jobject clazz, jlong dstPaintHandle, jlong srcPaintHandle) {
        Paint* dst = reinterpret_cast<Paint*>(dstPaintHandle);
        const Paint* src = reinterpret_cast<Paint*>(srcPaintHandle);
        *dst = *src;
    }

    // Equivalent to the Java Paint's FILTER_BITMAP_FLAG.
    static const uint32_t sFilterBitmapFlag = 0x02;

    static jint getFlags(JNIEnv* env, jobject, jlong paintHandle) {
        Paint* nativePaint = reinterpret_cast<Paint*>(paintHandle);
        uint32_t result = nativePaint->getFlags();
        result &= ~sFilterBitmapFlag; // Filtering no longer stored in this bit. Mask away.
        if (nativePaint->getFilterQuality() != kNone_SkFilterQuality) {
            result |= sFilterBitmapFlag;
        }
        return static_cast<jint>(result);
    }

    static void setFlags(JNIEnv* env, jobject, jlong paintHandle, jint flags) {
        Paint* nativePaint = reinterpret_cast<Paint*>(paintHandle);
        // Instead of modifying 0x02, change the filter level.
        nativePaint->setFilterQuality(flags & sFilterBitmapFlag
                ? kLow_SkFilterQuality
                : kNone_SkFilterQuality);
        // Don't pass through filter flag, which is no longer stored in paint's flags.
        flags &= ~sFilterBitmapFlag;
        // Use the existing value for 0x02.
        const uint32_t existing0x02Flag = nativePaint->getFlags() & sFilterBitmapFlag;
        flags |= existing0x02Flag;
        nativePaint->setFlags(flags);
    }

    static jint getHinting(JNIEnv* env, jobject, jlong paintHandle) {
        return reinterpret_cast<Paint*>(paintHandle)->getHinting()
                == Paint::kNo_Hinting ? 0 : 1;
    }

    static void setHinting(JNIEnv* env, jobject, jlong paintHandle, jint mode) {
        reinterpret_cast<Paint*>(paintHandle)->setHinting(
                mode == 0 ? Paint::kNo_Hinting : Paint::kNormal_Hinting);
    }

    static void setAntiAlias(JNIEnv* env, jobject, jlong paintHandle, jboolean aa) {
        reinterpret_cast<Paint*>(paintHandle)->setAntiAlias(aa);
    }

    static void setLinearText(JNIEnv* env, jobject, jlong paintHandle, jboolean linearText) {
        reinterpret_cast<Paint*>(paintHandle)->setLinearText(linearText);
    }

    static void setSubpixelText(JNIEnv* env, jobject, jlong paintHandle, jboolean subpixelText) {
        reinterpret_cast<Paint*>(paintHandle)->setSubpixelText(subpixelText);
    }

    static void setUnderlineText(JNIEnv* env, jobject, jlong paintHandle, jboolean underlineText) {
        reinterpret_cast<Paint*>(paintHandle)->setUnderlineText(underlineText);
    }

    static void setStrikeThruText(JNIEnv* env, jobject, jlong paintHandle, jboolean strikeThruText) {
        reinterpret_cast<Paint*>(paintHandle)->setStrikeThruText(strikeThruText);
    }

    static void setFakeBoldText(JNIEnv* env, jobject, jlong paintHandle, jboolean fakeBoldText) {
        reinterpret_cast<Paint*>(paintHandle)->setFakeBoldText(fakeBoldText);
    }

    static void setFilterBitmap(JNIEnv* env, jobject, jlong paintHandle, jboolean filterBitmap) {
        reinterpret_cast<Paint*>(paintHandle)->setFilterQuality(
                filterBitmap ? kLow_SkFilterQuality : kNone_SkFilterQuality);
    }

    static void setDither(JNIEnv* env, jobject, jlong paintHandle, jboolean dither) {
        reinterpret_cast<Paint*>(paintHandle)->setDither(dither);
    }

    static jint getStyle(JNIEnv* env, jobject clazz,jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        return static_cast<jint>(obj->getStyle());
    }

    static void setStyle(JNIEnv* env, jobject clazz, jlong objHandle, jint styleHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        Paint::Style style = static_cast<Paint::Style>(styleHandle);
        obj->setStyle(style);
    }

    static jint getColor(JNIEnv* env, jobject, jlong paintHandle) {
        int color;
        color = reinterpret_cast<Paint*>(paintHandle)->getColor();
        return static_cast<jint>(color);
    }

    static jint getAlpha(JNIEnv* env, jobject, jlong paintHandle) {
        int alpha;
        alpha = reinterpret_cast<Paint*>(paintHandle)->getAlpha();
        return static_cast<jint>(alpha);
    }

    static void setColor(JNIEnv* env, jobject, jlong paintHandle, jint color) {
        reinterpret_cast<Paint*>(paintHandle)->setColor(color);
    }

    static void setAlpha(JNIEnv* env, jobject, jlong paintHandle, jint a) {
        reinterpret_cast<Paint*>(paintHandle)->setAlpha(a);
    }

    static jfloat getStrokeWidth(JNIEnv* env, jobject, jlong paintHandle) {
        return SkScalarToFloat(reinterpret_cast<Paint*>(paintHandle)->getStrokeWidth());
    }

    static void setStrokeWidth(JNIEnv* env, jobject, jlong paintHandle, jfloat width) {
        reinterpret_cast<Paint*>(paintHandle)->setStrokeWidth(width);
    }

    static jfloat getStrokeMiter(JNIEnv* env, jobject, jlong paintHandle) {
        return SkScalarToFloat(reinterpret_cast<Paint*>(paintHandle)->getStrokeMiter());
    }

    static void setStrokeMiter(JNIEnv* env, jobject, jlong paintHandle, jfloat miter) {
        reinterpret_cast<Paint*>(paintHandle)->setStrokeMiter(miter);
    }

    static jint getStrokeCap(JNIEnv* env, jobject clazz, jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        return static_cast<jint>(obj->getStrokeCap());
    }

    static void setStrokeCap(JNIEnv* env, jobject clazz, jlong objHandle, jint capHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        Paint::Cap cap = static_cast<Paint::Cap>(capHandle);
        obj->setStrokeCap(cap);
    }

    static jint getStrokeJoin(JNIEnv* env, jobject clazz, jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        return static_cast<jint>(obj->getStrokeJoin());
    }

    static void setStrokeJoin(JNIEnv* env, jobject clazz, jlong objHandle, jint joinHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        Paint::Join join = (Paint::Join) joinHandle;
        obj->setStrokeJoin(join);
    }

    static jboolean getFillPath(JNIEnv* env, jobject clazz, jlong objHandle, jlong srcHandle, jlong dstHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkPath* src = reinterpret_cast<SkPath*>(srcHandle);
        SkPath* dst = reinterpret_cast<SkPath*>(dstHandle);
        return obj->getFillPath(*src, dst) ? JNI_TRUE : JNI_FALSE;
    }

    static jlong setShader(JNIEnv* env, jobject clazz, jlong objHandle, jlong shaderHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkShader* shader = reinterpret_cast<SkShader*>(shaderHandle);
        return reinterpret_cast<jlong>(obj->setShader(shader));
    }

    static jlong setColorFilter(JNIEnv* env, jobject clazz, jlong objHandle, jlong filterHandle) {
        Paint* obj = reinterpret_cast<Paint *>(objHandle);
        SkColorFilter* filter  = reinterpret_cast<SkColorFilter *>(filterHandle);
        return reinterpret_cast<jlong>(obj->setColorFilter(filter));
    }

    static jlong setXfermode(JNIEnv* env, jobject clazz, jlong objHandle, jlong xfermodeHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkXfermode* xfermode = reinterpret_cast<SkXfermode*>(xfermodeHandle);
        return reinterpret_cast<jlong>(obj->setXfermode(xfermode));
    }

    static jlong setPathEffect(JNIEnv* env, jobject clazz, jlong objHandle, jlong effectHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkPathEffect* effect  = reinterpret_cast<SkPathEffect*>(effectHandle);
        return reinterpret_cast<jlong>(obj->setPathEffect(effect));
    }

    static jlong setMaskFilter(JNIEnv* env, jobject clazz, jlong objHandle, jlong maskfilterHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkMaskFilter* maskfilter  = reinterpret_cast<SkMaskFilter*>(maskfilterHandle);
        return reinterpret_cast<jlong>(obj->setMaskFilter(maskfilter));
    }

    static jlong setTypeface(JNIEnv* env, jobject clazz, jlong objHandle, jlong typefaceHandle) {
        // TODO: in Paint refactoring, set typeface on android Paint, not Paint
        return NULL;
    }

    static jlong setRasterizer(JNIEnv* env, jobject clazz, jlong objHandle, jlong rasterizerHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        SkAutoTUnref<SkRasterizer> rasterizer(GraphicsJNI::refNativeRasterizer(rasterizerHandle));
        return reinterpret_cast<jlong>(obj->setRasterizer(rasterizer));
    }

    static jint getTextAlign(JNIEnv* env, jobject clazz, jlong objHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        return static_cast<jint>(obj->getTextAlign());
    }

    static void setTextAlign(JNIEnv* env, jobject clazz, jlong objHandle, jint alignHandle) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        Paint::Align align = static_cast<Paint::Align>(alignHandle);
        obj->setTextAlign(align);
    }

    // generate bcp47 identifier for the supplied locale
    static void toLanguageTag(char* output, size_t outSize,
            const char* locale) {
        if (output == NULL || outSize <= 0) {
            return;
        }
        if (locale == NULL) {
            output[0] = '\0';
            return;
        }
        char canonicalChars[ULOC_FULLNAME_CAPACITY];
        UErrorCode uErr = U_ZERO_ERROR;
        uloc_canonicalize(locale, canonicalChars, ULOC_FULLNAME_CAPACITY,
                &uErr);
        if (U_SUCCESS(uErr)) {
            char likelyChars[ULOC_FULLNAME_CAPACITY];
            uErr = U_ZERO_ERROR;
            uloc_addLikelySubtags(canonicalChars, likelyChars,
                    ULOC_FULLNAME_CAPACITY, &uErr);
            if (U_SUCCESS(uErr)) {
                uErr = U_ZERO_ERROR;
                uloc_toLanguageTag(likelyChars, output, outSize, FALSE, &uErr);
                if (U_SUCCESS(uErr)) {
                    return;
                } else {
                    ALOGD("uloc_toLanguageTag(\"%s\") failed: %s", likelyChars,
                            u_errorName(uErr));
                }
            } else {
                ALOGD("uloc_addLikelySubtags(\"%s\") failed: %s",
                        canonicalChars, u_errorName(uErr));
            }
        } else {
            ALOGD("uloc_canonicalize(\"%s\") failed: %s", locale,
                    u_errorName(uErr));
        }
        // unable to build a proper language identifier
        output[0] = '\0';
    }

    static void setTextLocale(JNIEnv* env, jobject clazz, jlong objHandle, jstring locale) {
        Paint* obj = reinterpret_cast<Paint*>(objHandle);
        ScopedUtfChars localeChars(env, locale);
        if (sSingleEntryLocaleCache.javaLocale != localeChars.c_str()) {
            sSingleEntryLocaleCache.javaLocale = localeChars.c_str();
            char langTag[ULOC_FULLNAME_CAPACITY];
            toLanguageTag(langTag, ULOC_FULLNAME_CAPACITY, localeChars.c_str());
            sSingleEntryLocaleCache.languageTag = langTag;
        }

        obj->setTextLocale(sSingleEntryLocaleCache.languageTag);
    }

    static jboolean isElegantTextHeight(JNIEnv* env, jobject, jlong paintHandle) {
        Paint* obj = reinterpret_cast<Paint*>(paintHandle);
        return obj->getFontVariant() == VARIANT_ELEGANT;
    }

    static void setElegantTextHeight(JNIEnv* env, jobject, jlong paintHandle, jboolean aa) {
        Paint* obj = reinterpret_cast<Paint*>(paintHandle);
        obj->setFontVariant(aa ? VARIANT_ELEGANT : VARIANT_DEFAULT);
    }

    static jfloat getTextSize(JNIEnv* env, jobject, jlong paintHandle) {
        return SkScalarToFloat(reinterpret_cast<Paint*>(paintHandle)->getTextSize());
    }

    static void setTextSize(JNIEnv* env, jobject, jlong paintHandle, jfloat textSize) {
        reinterpret_cast<Paint*>(paintHandle)->setTextSize(textSize);
    }

    static jfloat getTextScaleX(JNIEnv* env, jobject, jlong paintHandle) {
        return SkScalarToFloat(reinterpret_cast<Paint*>(paintHandle)->getTextScaleX());
    }

    static void setTextScaleX(JNIEnv* env, jobject, jlong paintHandle, jfloat scaleX) {
        reinterpret_cast<Paint*>(paintHandle)->setTextScaleX(scaleX);
    }

    static jfloat getTextSkewX(JNIEnv* env, jobject, jlong paintHandle) {
        return SkScalarToFloat(reinterpret_cast<Paint*>(paintHandle)->getTextSkewX());
    }

    static void setTextSkewX(JNIEnv* env, jobject, jlong paintHandle, jfloat skewX) {
        reinterpret_cast<Paint*>(paintHandle)->setTextSkewX(skewX);
    }

    static jfloat getLetterSpacing(JNIEnv* env, jobject clazz, jlong paintHandle) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        return paint->getLetterSpacing();
    }

    static void setLetterSpacing(JNIEnv* env, jobject clazz, jlong paintHandle, jfloat letterSpacing) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        paint->setLetterSpacing(letterSpacing);
    }

    static void setFontFeatureSettings(JNIEnv* env, jobject clazz, jlong paintHandle, jstring settings) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        if (!settings) {
            paint->setFontFeatureSettings(std::string());
        } else {
            ScopedUtfChars settingsChars(env, settings);
            paint->setFontFeatureSettings(std::string(settingsChars.c_str(), settingsChars.size()));
        }
    }

    static jint getHyphenEdit(JNIEnv* env, jobject clazz, jlong paintHandle, jint hyphen) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        return paint->getHyphenEdit();
    }

    static void setHyphenEdit(JNIEnv* env, jobject clazz, jlong paintHandle, jint hyphen) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        paint->setHyphenEdit((uint32_t)hyphen);
    }

    static SkScalar getMetricsInternal(jlong paintHandle, jlong typefaceHandle,
            Paint::FontMetrics *metrics) {
        const int kElegantTop = 2500;
        const int kElegantBottom = -1000;
        const int kElegantAscent = 1900;
        const int kElegantDescent = -500;
        const int kElegantLeading = 0;
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        typeface = TypefaceImpl_resolveDefault(typeface);
        FakedFont baseFont = typeface->fFontCollection->baseFontFaked(typeface->fStyle);
        float saveSkewX = paint->getTextSkewX();
        bool savefakeBold = paint->isFakeBoldText();
        MinikinFontSkia::populateSkPaint(paint, baseFont.font, baseFont.fakery);
        SkScalar spacing = paint->getFontMetrics(metrics);
        // The populateSkPaint call may have changed fake bold / text skew
        // because we want to measure with those effects applied, so now
        // restore the original settings.
        paint->setTextSkewX(saveSkewX);
        paint->setFakeBoldText(savefakeBold);
        if (paint->getFontVariant() == VARIANT_ELEGANT) {
            SkScalar size = paint->getTextSize();
            metrics->fTop = -size * kElegantTop / 2048;
            metrics->fBottom = -size * kElegantBottom / 2048;
            metrics->fAscent = -size * kElegantAscent / 2048;
            metrics->fDescent = -size * kElegantDescent / 2048;
            metrics->fLeading = size * kElegantLeading / 2048;
            spacing = metrics->fDescent - metrics->fAscent + metrics->fLeading;
        }
        return spacing;
    }

    static jfloat ascent(JNIEnv* env, jobject, jlong paintHandle, jlong typefaceHandle) {
        Paint::FontMetrics metrics;
        getMetricsInternal(paintHandle, typefaceHandle, &metrics);
        return SkScalarToFloat(metrics.fAscent);
    }

    static jfloat descent(JNIEnv* env, jobject, jlong paintHandle, jlong typefaceHandle) {
        Paint::FontMetrics metrics;
        getMetricsInternal(paintHandle, typefaceHandle, &metrics);
        return SkScalarToFloat(metrics.fDescent);
    }

    static jfloat getFontMetrics(JNIEnv* env, jobject, jlong paintHandle,
            jlong typefaceHandle, jobject metricsObj) {
        Paint::FontMetrics metrics;
        SkScalar spacing = getMetricsInternal(paintHandle, typefaceHandle, &metrics);

        if (metricsObj) {
            SkASSERT(env->IsInstanceOf(metricsObj, gFontMetrics_class));
            env->SetFloatField(metricsObj, gFontMetrics_fieldID.top, SkScalarToFloat(metrics.fTop));
            env->SetFloatField(metricsObj, gFontMetrics_fieldID.ascent, SkScalarToFloat(metrics.fAscent));
            env->SetFloatField(metricsObj, gFontMetrics_fieldID.descent, SkScalarToFloat(metrics.fDescent));
            env->SetFloatField(metricsObj, gFontMetrics_fieldID.bottom, SkScalarToFloat(metrics.fBottom));
            env->SetFloatField(metricsObj, gFontMetrics_fieldID.leading, SkScalarToFloat(metrics.fLeading));
        }
        return SkScalarToFloat(spacing);
    }

    static jint getFontMetricsInt(JNIEnv* env, jobject, jlong paintHandle,
            jlong typefaceHandle, jobject metricsObj) {
        Paint::FontMetrics metrics;

        getMetricsInternal(paintHandle, typefaceHandle, &metrics);
        int ascent = SkScalarRoundToInt(metrics.fAscent);
        int descent = SkScalarRoundToInt(metrics.fDescent);
        int leading = SkScalarRoundToInt(metrics.fLeading);

        if (metricsObj) {
            SkASSERT(env->IsInstanceOf(metricsObj, gFontMetricsInt_class));
            env->SetIntField(metricsObj, gFontMetricsInt_fieldID.top, SkScalarFloorToInt(metrics.fTop));
            env->SetIntField(metricsObj, gFontMetricsInt_fieldID.ascent, ascent);
            env->SetIntField(metricsObj, gFontMetricsInt_fieldID.descent, descent);
            env->SetIntField(metricsObj, gFontMetricsInt_fieldID.bottom, SkScalarCeilToInt(metrics.fBottom));
            env->SetIntField(metricsObj, gFontMetricsInt_fieldID.leading, leading);
        }
        return descent - ascent + leading;
    }

    static jfloat doTextAdvances(JNIEnv *env, Paint *paint, TypefaceImpl* typeface,
            const jchar *text, jint start, jint count, jint contextCount, jint bidiFlags,
            jfloatArray advances, jint advancesIndex) {
        NPE_CHECK_RETURN_ZERO(env, text);

        if ((start | count | contextCount | advancesIndex) < 0 || contextCount < count) {
            doThrowAIOOBE(env);
            return 0;
        }
        if (count == 0) {
            return 0;
        }
        if (advances) {
            size_t advancesLength = env->GetArrayLength(advances);
            if ((size_t)(count  + advancesIndex) > advancesLength) {
                doThrowAIOOBE(env);
                return 0;
            }
        }

        Layout layout;
        MinikinUtils::doLayout(&layout, paint, bidiFlags, typeface, text, start, count,
                contextCount);
        if (advances != NULL) {
            std::unique_ptr<jfloat> advancesArray(new jfloat[count]);
            layout.getAdvances(advancesArray.get());
            env->SetFloatArrayRegion(advances, advancesIndex, count, advancesArray.get());
        }
        return layout.getAdvance();
    }

    static jfloat getTextAdvances___CIIIII_FI(JNIEnv* env, jobject clazz, jlong paintHandle,
            jlong typefaceHandle,
            jcharArray text, jint index, jint count, jint contextIndex, jint contextCount,
            jint bidiFlags, jfloatArray advances, jint advancesIndex) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        jchar* textArray = env->GetCharArrayElements(text, NULL);
        jfloat result = doTextAdvances(env, paint, typeface, textArray + contextIndex,
                index - contextIndex, count, contextCount, bidiFlags, advances, advancesIndex);
        env->ReleaseCharArrayElements(text, textArray, JNI_ABORT);
        return result;
    }

    static jfloat getTextAdvances__StringIIIII_FI(JNIEnv* env, jobject clazz, jlong paintHandle,
            jlong typefaceHandle,
            jstring text, jint start, jint end, jint contextStart, jint contextEnd, jint bidiFlags,
            jfloatArray advances, jint advancesIndex) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        const jchar* textArray = env->GetStringChars(text, NULL);
        jfloat result = doTextAdvances(env, paint, typeface, textArray + contextStart,
                start - contextStart, end - start, contextEnd - contextStart, bidiFlags,
                advances, advancesIndex);
        env->ReleaseStringChars(text, textArray);
        return result;
    }

    static jint doTextRunCursor(JNIEnv *env, Paint* paint, const jchar *text, jint start,
            jint count, jint flags, jint offset, jint opt) {
        GraphemeBreak::MoveOpt moveOpt = GraphemeBreak::MoveOpt(opt);
        size_t result = GraphemeBreak::getTextRunCursor(text, start, count, offset, moveOpt);
        return static_cast<jint>(result);
    }

    static jint getTextRunCursor___C(JNIEnv* env, jobject clazz, jlong paintHandle, jcharArray text,
            jint contextStart, jint contextCount, jint dir, jint offset, jint cursorOpt) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        jchar* textArray = env->GetCharArrayElements(text, NULL);
        jint result = doTextRunCursor(env, paint, textArray, contextStart, contextCount, dir,
                offset, cursorOpt);
        env->ReleaseCharArrayElements(text, textArray, JNI_ABORT);
        return result;
    }

    static jint getTextRunCursor__String(JNIEnv* env, jobject clazz, jlong paintHandle, jstring text,
            jint contextStart, jint contextEnd, jint dir, jint offset, jint cursorOpt) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        const jchar* textArray = env->GetStringChars(text, NULL);
        jint result = doTextRunCursor(env, paint, textArray, contextStart,
                contextEnd - contextStart, dir, offset, cursorOpt);
        env->ReleaseStringChars(text, textArray);
        return result;
    }

    class GetTextFunctor {
    public:
        GetTextFunctor(const Layout& layout, SkPath* path, jfloat x, jfloat y, Paint* paint,
                    uint16_t* glyphs, SkPoint* pos)
                : layout(layout), path(path), x(x), y(y), paint(paint), glyphs(glyphs), pos(pos) {
        }

        void operator()(size_t start, size_t end) {
            for (size_t i = start; i < end; i++) {
                glyphs[i] = layout.getGlyphId(i);
                pos[i].fX = x + layout.getX(i);
                pos[i].fY = y + layout.getY(i);
            }
            if (start == 0) {
                paint->getPosTextPath(glyphs + start, (end - start) << 1, pos + start, path);
            } else {
                paint->getPosTextPath(glyphs + start, (end - start) << 1, pos + start, &tmpPath);
                path->addPath(tmpPath);
            }
        }
    private:
        const Layout& layout;
        SkPath* path;
        jfloat x;
        jfloat y;
        Paint* paint;
        uint16_t* glyphs;
        SkPoint* pos;
        SkPath tmpPath;
    };

    static void getTextPath(JNIEnv* env, Paint* paint, TypefaceImpl* typeface, const jchar* text,
            jint count, jint bidiFlags, jfloat x, jfloat y, SkPath* path) {
        Layout layout;
        MinikinUtils::doLayout(&layout, paint, bidiFlags, typeface, text, 0, count, count);
        size_t nGlyphs = layout.nGlyphs();
        uint16_t* glyphs = new uint16_t[nGlyphs];
        SkPoint* pos = new SkPoint[nGlyphs];

        x += MinikinUtils::xOffsetForTextAlign(paint, layout);
        Paint::Align align = paint->getTextAlign();
        paint->setTextAlign(Paint::kLeft_Align);
        paint->setTextEncoding(Paint::kGlyphID_TextEncoding);
        GetTextFunctor f(layout, path, x, y, paint, glyphs, pos);
        MinikinUtils::forFontRun(layout, paint, f);
        paint->setTextAlign(align);
        delete[] glyphs;
        delete[] pos;
    }

    static void getTextPath___C(JNIEnv* env, jobject clazz, jlong paintHandle,
            jlong typefaceHandle, jint bidiFlags,
            jcharArray text, jint index, jint count, jfloat x, jfloat y, jlong pathHandle) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        SkPath* path = reinterpret_cast<SkPath*>(pathHandle);
        const jchar* textArray = env->GetCharArrayElements(text, NULL);
        getTextPath(env, paint, typeface, textArray + index, count, bidiFlags, x, y, path);
        env->ReleaseCharArrayElements(text, const_cast<jchar*>(textArray), JNI_ABORT);
    }

    static void getTextPath__String(JNIEnv* env, jobject clazz, jlong paintHandle,
            jlong typefaceHandle, jint bidiFlags,
            jstring text, jint start, jint end, jfloat x, jfloat y, jlong pathHandle) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        SkPath* path = reinterpret_cast<SkPath*>(pathHandle);
        const jchar* textArray = env->GetStringChars(text, NULL);
        getTextPath(env, paint, typeface, textArray + start, end - start, bidiFlags, x, y, path);
        env->ReleaseStringChars(text, textArray);
    }

    static void setShadowLayer(JNIEnv* env, jobject clazz, jlong paintHandle, jfloat radius,
                               jfloat dx, jfloat dy, jint color) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        if (radius <= 0) {
            paint->setLooper(NULL);
        }
        else {
            SkScalar sigma = android::uirenderer::Blur::convertRadiusToSigma(radius);
            paint->setLooper(SkBlurDrawLooper::Create((SkColor)color, sigma, dx, dy))->unref();
        }
    }

    static jboolean hasShadowLayer(JNIEnv* env, jobject clazz, jlong paintHandle) {
        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        return paint->getLooper() && paint->getLooper()->asABlurShadow(NULL);
    }

    static int breakText(JNIEnv* env, const Paint& paint, TypefaceImpl* typeface, const jchar text[],
                         int count, float maxWidth, jint bidiFlags, jfloatArray jmeasured,
                         const bool forwardScan) {
        size_t measuredCount = 0;
        float measured = 0;

        Layout layout;
        MinikinUtils::doLayout(&layout, &paint, bidiFlags, typeface, text, 0, count, count);
        float* advances = new float[count];
        layout.getAdvances(advances);

        for (int i = 0; i < count; i++) {
            // traverse in the given direction
            int index = forwardScan ? i : (count - i - 1);
            float width = advances[index];
            if (measured + width > maxWidth) {
                break;
            }
            // properly handle clusters when scanning backwards
            if (forwardScan || width != 0.0f) {
                measuredCount = i + 1;
            }
            measured += width;
        }
        delete[] advances;

        if (jmeasured && env->GetArrayLength(jmeasured) > 0) {
            AutoJavaFloatArray autoMeasured(env, jmeasured, 1);
            jfloat* array = autoMeasured.ptr();
            array[0] = measured;
        }
        return measuredCount;
    }

    static jint breakTextC(JNIEnv* env, jobject clazz, jlong paintHandle, jlong typefaceHandle, jcharArray jtext,
            jint index, jint count, jfloat maxWidth, jint bidiFlags, jfloatArray jmeasuredWidth) {
        NPE_CHECK_RETURN_ZERO(env, jtext);

        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);

        bool forwardTextDirection;
        if (count < 0) {
            forwardTextDirection = false;
            count = -count;
        }
        else {
            forwardTextDirection = true;
        }

        if ((index < 0) || (index + count > env->GetArrayLength(jtext))) {
            doThrowAIOOBE(env);
            return 0;
        }

        const jchar* text = env->GetCharArrayElements(jtext, NULL);
        count = breakText(env, *paint, typeface, text + index, count, maxWidth,
                          bidiFlags, jmeasuredWidth, forwardTextDirection);
        env->ReleaseCharArrayElements(jtext, const_cast<jchar*>(text),
                                      JNI_ABORT);
        return count;
    }

    static jint breakTextS(JNIEnv* env, jobject clazz, jlong paintHandle, jlong typefaceHandle, jstring jtext,
                jboolean forwards, jfloat maxWidth, jint bidiFlags, jfloatArray jmeasuredWidth) {
        NPE_CHECK_RETURN_ZERO(env, jtext);

        Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);

        int count = env->GetStringLength(jtext);
        const jchar* text = env->GetStringChars(jtext, NULL);
        count = breakText(env, *paint, typeface, text, count, maxWidth, bidiFlags, jmeasuredWidth, forwards);
        env->ReleaseStringChars(jtext, text);
        return count;
    }

    static void doTextBounds(JNIEnv* env, const jchar* text, int count, jobject bounds,
            const Paint& paint, TypefaceImpl* typeface, jint bidiFlags) {
        SkRect  r;
        SkIRect ir;

        Layout layout;
        MinikinUtils::doLayout(&layout, &paint, bidiFlags, typeface, text, 0, count, count);
        MinikinRect rect;
        layout.getBounds(&rect);
        r.fLeft = rect.mLeft;
        r.fTop = rect.mTop;
        r.fRight = rect.mRight;
        r.fBottom = rect.mBottom;
        r.roundOut(&ir);
        GraphicsJNI::irect_to_jrect(ir, env, bounds);
    }

    static void getStringBounds(JNIEnv* env, jobject, jlong paintHandle, jlong typefaceHandle,
                                jstring text, jint start, jint end, jint bidiFlags, jobject bounds) {
        const Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        const jchar* textArray = env->GetStringChars(text, NULL);
        doTextBounds(env, textArray + start, end - start, bounds, *paint, typeface, bidiFlags);
        env->ReleaseStringChars(text, textArray);
    }

    static void getCharArrayBounds(JNIEnv* env, jobject, jlong paintHandle, jlong typefaceHandle,
                        jcharArray text, jint index, jint count, jint bidiFlags, jobject bounds) {
        const Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        const jchar* textArray = env->GetCharArrayElements(text, NULL);
        doTextBounds(env, textArray + index, count, bounds, *paint, typeface, bidiFlags);
        env->ReleaseCharArrayElements(text, const_cast<jchar*>(textArray),
                                      JNI_ABORT);
    }

    static jboolean layoutContainsNotdef(const Layout& layout) {
        for (size_t i = 0; i < layout.nGlyphs(); i++) {
            if (layout.getGlyphId(i) == 0) {
                return true;
            }
        }
        return false;
    }

    static jboolean hasGlyphVariation(const Paint* paint, TypefaceImpl* typeface, jint bidiFlags,
            const jchar* chars, size_t size) {
        // TODO: query font for whether character has variation selector; requires a corresponding
        // function in Minikin.
        return false;
    }

    static jboolean hasGlyph(JNIEnv *env, jclass, jlong paintHandle, jlong typefaceHandle,
            jint bidiFlags, jstring string) {
        const Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        ScopedStringChars str(env, string);

        /* start by rejecting variation selectors (not supported yet) */
        size_t nChars = 0;
        for (size_t i = 0; i < str.size(); i++) {
            jchar c = str[i];
            if (0xDC00 <= c && c <= 0xDFFF) {
                // invalid UTF-16, unpaired trailing surrogate
                return false;
            } else if (0xD800 <= c && c <= 0xDBFF) {
                if (i + 1 == str.size()) {
                    // invalid UTF-16, unpaired leading surrogate at end of string
                    return false;
                }
                i++;
                jchar c2 = str[i];
                if (!(0xDC00 <= c2 && c2 <= 0xDFFF)) {
                    // invalid UTF-16, unpaired leading surrogate
                    return false;
                }
                // UTF-16 encoding of range U+E0100..U+E01EF is DB40 DD00 .. DB40 DDEF
                if (c == 0xDB40 && 0xDD00 <= c2 && c2 <= 0xDDEF) {
                    return hasGlyphVariation(paint, typeface, bidiFlags, str.get(), str.size());
                }
            } else if (0xFE00 <= c && c <= 0xFE0F) {
                return hasGlyphVariation(paint, typeface, bidiFlags, str.get(), str.size());
            }
            nChars++;
        }
        Layout layout;
        MinikinUtils::doLayout(&layout, paint, bidiFlags, typeface, str.get(), 0, str.size(),
                str.size());
        size_t nGlyphs = layout.nGlyphs();
        if (nGlyphs != 1 && nChars > 1) {
            // multiple-character input, and was not a ligature
            // TODO: handle ZWJ/ZWNJ characters specially so we can detect certain ligatures
            // in joining scripts, such as Arabic and Mongolian.
            return false;
        }
        return nGlyphs > 0 && !layoutContainsNotdef(layout);
    }

    static jfloat doRunAdvance(const Paint* paint, TypefaceImpl* typeface, const jchar buf[],
            jint start, jint count, jint bufSize, jboolean isRtl, jint offset) {
        Layout layout;
        int bidiFlags = isRtl ? kBidi_Force_RTL : kBidi_Force_LTR;
        MinikinUtils::doLayout(&layout, paint, bidiFlags, typeface, buf, start, count, bufSize);
        return getRunAdvance(layout, buf, start, count, offset);
    }

    static jfloat getRunAdvance___CIIIIZI_F(JNIEnv *env, jclass, jlong paintHandle,
            jlong typefaceHandle, jcharArray text, jint start, jint end, jint contextStart,
            jint contextEnd, jboolean isRtl, jint offset) {
        const Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        jchar* textArray = (jchar*) env->GetPrimitiveArrayCritical(text, NULL);
        jfloat result = doRunAdvance(paint, typeface, textArray + contextStart,
                start - contextStart, end - start, contextEnd - contextStart, isRtl,
                offset - contextStart);
        env->ReleasePrimitiveArrayCritical(text, textArray, JNI_ABORT);
        return result;
    }

    static jint doOffsetForAdvance(const Paint* paint, TypefaceImpl* typeface, const jchar buf[],
            jint start, jint count, jint bufSize, jboolean isRtl, jfloat advance) {
        Layout layout;
        int bidiFlags = isRtl ? kBidi_Force_RTL : kBidi_Force_LTR;
        MinikinUtils::doLayout(&layout, paint, bidiFlags, typeface, buf, start, count, bufSize);
        return getOffsetForAdvance(layout, buf, start, count, advance);
    }
    static jint getOffsetForAdvance___CIIIIZF_I(JNIEnv *env, jclass, jlong paintHandle,
            jlong typefaceHandle, jcharArray text, jint start, jint end, jint contextStart,
            jint contextEnd, jboolean isRtl, jfloat advance) {
        const Paint* paint = reinterpret_cast<Paint*>(paintHandle);
        TypefaceImpl* typeface = reinterpret_cast<TypefaceImpl*>(typefaceHandle);
        jchar* textArray = (jchar*) env->GetPrimitiveArrayCritical(text, NULL);
        jint result = doOffsetForAdvance(paint, typeface, textArray + contextStart,
                start - contextStart, end - start, contextEnd - contextStart, isRtl, advance);
        result += contextStart;
        env->ReleasePrimitiveArrayCritical(text, textArray, JNI_ABORT);
        return result;
    }

}; // namespace PaintGlue

static const JNINativeMethod methods[] = {
    {"nFinalizer", "(J)V", (void*) PaintGlue::finalizer},
    {"nInit","()J", (void*) PaintGlue::init},
    {"nInitWithPaint","(J)J", (void*) PaintGlue::initWithPaint},

    {"nReset","!(J)V", (void*) PaintGlue::reset},
    {"nSet","!(JJ)V", (void*) PaintGlue::assign},
    {"nGetFlags","!(J)I", (void*) PaintGlue::getFlags},
    {"nSetFlags","!(JI)V", (void*) PaintGlue::setFlags},
    {"nGetHinting","!(J)I", (void*) PaintGlue::getHinting},
    {"nSetHinting","!(JI)V", (void*) PaintGlue::setHinting},
    {"nSetAntiAlias","!(JZ)V", (void*) PaintGlue::setAntiAlias},
    {"nSetSubpixelText","!(JZ)V", (void*) PaintGlue::setSubpixelText},
    {"nSetLinearText","!(JZ)V", (void*) PaintGlue::setLinearText},
    {"nSetUnderlineText","!(JZ)V", (void*) PaintGlue::setUnderlineText},
    {"nSetStrikeThruText","!(JZ)V", (void*) PaintGlue::setStrikeThruText},
    {"nSetFakeBoldText","!(JZ)V", (void*) PaintGlue::setFakeBoldText},
    {"nSetFilterBitmap","!(JZ)V", (void*) PaintGlue::setFilterBitmap},
    {"nSetDither","!(JZ)V", (void*) PaintGlue::setDither},
    {"nGetStyle","!(J)I", (void*) PaintGlue::getStyle},
    {"nSetStyle","!(JI)V", (void*) PaintGlue::setStyle},
    {"nGetColor","!(J)I", (void*) PaintGlue::getColor},
    {"nSetColor","!(JI)V", (void*) PaintGlue::setColor},
    {"nGetAlpha","!(J)I", (void*) PaintGlue::getAlpha},
    {"nSetAlpha","!(JI)V", (void*) PaintGlue::setAlpha},
    {"nGetStrokeWidth","!(J)F", (void*) PaintGlue::getStrokeWidth},
    {"nSetStrokeWidth","!(JF)V", (void*) PaintGlue::setStrokeWidth},
    {"nGetStrokeMiter","!(J)F", (void*) PaintGlue::getStrokeMiter},
    {"nSetStrokeMiter","!(JF)V", (void*) PaintGlue::setStrokeMiter},
    {"nGetStrokeCap","!(J)I", (void*) PaintGlue::getStrokeCap},
    {"nSetStrokeCap","!(JI)V", (void*) PaintGlue::setStrokeCap},
    {"nGetStrokeJoin","!(J)I", (void*) PaintGlue::getStrokeJoin},
    {"nSetStrokeJoin","!(JI)V", (void*) PaintGlue::setStrokeJoin},
    {"nGetFillPath","!(JJJ)Z", (void*) PaintGlue::getFillPath},
    {"nSetShader","!(JJ)J", (void*) PaintGlue::setShader},
    {"nSetColorFilter","!(JJ)J", (void*) PaintGlue::setColorFilter},
    {"nSetXfermode","!(JJ)J", (void*) PaintGlue::setXfermode},
    {"nSetPathEffect","!(JJ)J", (void*) PaintGlue::setPathEffect},
    {"nSetMaskFilter","!(JJ)J", (void*) PaintGlue::setMaskFilter},
    {"nSetTypeface","!(JJ)J", (void*) PaintGlue::setTypeface},
    {"nSetRasterizer","!(JJ)J", (void*) PaintGlue::setRasterizer},
    {"nGetTextAlign","!(J)I", (void*) PaintGlue::getTextAlign},
    {"nSetTextAlign","!(JI)V", (void*) PaintGlue::setTextAlign},
    {"nSetTextLocale","!(JLjava/lang/String;)V", (void*) PaintGlue::setTextLocale},
    {"nIsElegantTextHeight","!(J)Z", (void*) PaintGlue::isElegantTextHeight},
    {"nSetElegantTextHeight","!(JZ)V", (void*) PaintGlue::setElegantTextHeight},
    {"nGetTextSize","!(J)F", (void*) PaintGlue::getTextSize},
    {"nSetTextSize","!(JF)V", (void*) PaintGlue::setTextSize},
    {"nGetTextScaleX","!(J)F", (void*) PaintGlue::getTextScaleX},
    {"nSetTextScaleX","!(JF)V", (void*) PaintGlue::setTextScaleX},
    {"nGetTextSkewX","!(J)F", (void*) PaintGlue::getTextSkewX},
    {"nSetTextSkewX","!(JF)V", (void*) PaintGlue::setTextSkewX},
    {"nGetLetterSpacing","!(J)F", (void*) PaintGlue::getLetterSpacing},
    {"nSetLetterSpacing","!(JF)V", (void*) PaintGlue::setLetterSpacing},
    {"nSetFontFeatureSettings","(JLjava/lang/String;)V",
            (void*) PaintGlue::setFontFeatureSettings},
    {"nGetHyphenEdit", "!(J)I", (void*) PaintGlue::getHyphenEdit},
    {"nSetHyphenEdit", "!(JI)V", (void*) PaintGlue::setHyphenEdit},
    {"nAscent","!(JJ)F", (void*) PaintGlue::ascent},
    {"nDescent","!(JJ)F", (void*) PaintGlue::descent},

    {"nGetFontMetrics", "!(JJLandroid/graphics/Paint$FontMetrics;)F",
            (void*)PaintGlue::getFontMetrics},
    {"nGetFontMetricsInt", "!(JJLandroid/graphics/Paint$FontMetricsInt;)I",
            (void*)PaintGlue::getFontMetricsInt},

    {"nBreakText","(JJ[CIIFI[F)I", (void*) PaintGlue::breakTextC},
    {"nBreakText","(JJLjava/lang/String;ZFI[F)I", (void*) PaintGlue::breakTextS},
    {"nGetTextAdvances","(JJ[CIIIII[FI)F",
            (void*) PaintGlue::getTextAdvances___CIIIII_FI},
    {"nGetTextAdvances","(JJLjava/lang/String;IIIII[FI)F",
            (void*) PaintGlue::getTextAdvances__StringIIIII_FI},

    {"nGetTextRunCursor", "(J[CIIIII)I", (void*) PaintGlue::getTextRunCursor___C},
    {"nGetTextRunCursor", "(JLjava/lang/String;IIIII)I",
            (void*) PaintGlue::getTextRunCursor__String},
    {"nGetTextPath", "(JJI[CIIFFJ)V", (void*) PaintGlue::getTextPath___C},
    {"nGetTextPath", "(JJILjava/lang/String;IIFFJ)V", (void*) PaintGlue::getTextPath__String},
    {"nGetStringBounds", "(JJLjava/lang/String;IIILandroid/graphics/Rect;)V",
            (void*) PaintGlue::getStringBounds },
    {"nGetCharArrayBounds", "(JJ[CIIILandroid/graphics/Rect;)V",
            (void*) PaintGlue::getCharArrayBounds },
    {"nHasGlyph", "(JJILjava/lang/String;)Z", (void*) PaintGlue::hasGlyph },
    {"nGetRunAdvance", "(JJ[CIIIIZI)F", (void*) PaintGlue::getRunAdvance___CIIIIZI_F},
    {"nGetOffsetForAdvance", "(JJ[CIIIIZF)I",
            (void*) PaintGlue::getOffsetForAdvance___CIIIIZF_I},

    {"nSetShadowLayer", "!(JFFFI)V", (void*)PaintGlue::setShadowLayer},
    {"nHasShadowLayer", "!(J)Z", (void*)PaintGlue::hasShadowLayer}
};

int register_android_graphics_Paint(JNIEnv* env) {
    gFontMetrics_class = FindClassOrDie(env, "android/graphics/Paint$FontMetrics");
    gFontMetrics_class = MakeGlobalRefOrDie(env, gFontMetrics_class);

    gFontMetrics_fieldID.top = GetFieldIDOrDie(env, gFontMetrics_class, "top", "F");
    gFontMetrics_fieldID.ascent = GetFieldIDOrDie(env, gFontMetrics_class, "ascent", "F");
    gFontMetrics_fieldID.descent = GetFieldIDOrDie(env, gFontMetrics_class, "descent", "F");
    gFontMetrics_fieldID.bottom = GetFieldIDOrDie(env, gFontMetrics_class, "bottom", "F");
    gFontMetrics_fieldID.leading = GetFieldIDOrDie(env, gFontMetrics_class, "leading", "F");

    gFontMetricsInt_class = FindClassOrDie(env, "android/graphics/Paint$FontMetricsInt");
    gFontMetricsInt_class = MakeGlobalRefOrDie(env, gFontMetricsInt_class);

    gFontMetricsInt_fieldID.top = GetFieldIDOrDie(env, gFontMetricsInt_class, "top", "I");
    gFontMetricsInt_fieldID.ascent = GetFieldIDOrDie(env, gFontMetricsInt_class, "ascent", "I");
    gFontMetricsInt_fieldID.descent = GetFieldIDOrDie(env, gFontMetricsInt_class, "descent", "I");
    gFontMetricsInt_fieldID.bottom = GetFieldIDOrDie(env, gFontMetricsInt_class, "bottom", "I");
    gFontMetricsInt_fieldID.leading = GetFieldIDOrDie(env, gFontMetricsInt_class, "leading", "I");

    return RegisterMethodsOrDie(env, "android/graphics/Paint", methods, NELEM(methods));
}

}
