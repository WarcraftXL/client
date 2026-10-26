#ifndef ENGINE_SOURCE_FRAMEXML_LOADXML_H
#define ENGINE_SOURCE_FRAMEXML_LOADXML_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/Types.h"
#include "Engine/Source/Gx/Types.h"

class C2Vector;
class CImVector;
class CScriptRegion;
class CSimpleAnimGroup;
class CSimpleFontString;
class CSimpleFrame;
class CSimpleTexture;
class CStatus;
class XMLNode;
struct MD5_CTX;
struct lua_State;

struct SMOOTHWEIGHTS {
    /* 0000 */ float m_in;
    /* 0004 */ float m_out;
    /* 0008 */ const char* m_name;
};

struct XMLENUMVALUE {
    /* 0000 */ uint32_t m_value;
    /* 0004 */ const char* m_name;
};

const char* BlendModeToString(EGxBlend blend);
const char* CurveTypeToString(SimpleAnimCurveType curve);
const char* DrawLayerToString(uint32_t layer);
const char* FontFlagsToString(uint32_t flags);
const char* FramePointToString(FRAMEPOINT point);
const char* FrameStrataToString(FRAME_STRATA strata);
void CalculateSignatureMD5(const char* tocFile, const char* extraFile, uint8_t* digest);
int32_t FrameXML_CheckSignature(const char* tocFile, const char* extraFile, const uint8_t* modulus, uint8_t* digest);
const char* JustifyToString(uint32_t justify);
CSimpleAnimGroup* LoadXML_AnimGroup(XMLNode* node, CScriptRegion* region, CStatus* status);
int32_t LoadXML_AnimOrigin(XMLNode* node, FRAMEPOINT& point, C2Vector& offset, CStatus* status);
int32_t LoadXML_Color(XMLNode* node, CImVector& color, CStatus* status);
int32_t LoadXML_Dimensions(XMLNode* node, float& x, float& y, CStatus* status);
int32_t LoadXML_Gradient(XMLNode* node, ORIENTATION& orientation, CImVector& minColor, CImVector& maxColor, CStatus* status);
int32_t LoadXML_Insets(XMLNode* node, float& left, float& right, float& top, float& bottom, CStatus* status);
CSimpleFontString* LoadXML_String(XMLNode* node, CSimpleFrame* frame, CStatus* status);
CSimpleTexture* LoadXML_Texture(XMLNode* node, CSimpleFrame* frame, CStatus* status);
int32_t LoadXML_Value(XMLNode* node, float& value, CStatus* status);
const char* LoopStateToString(SimpleAnimLoopState state);
const char* LoopTypeToString(SimpleAnimLoopType loop);
const char* OrientationToString(ORIENTATION orientation);
int32_t ProcessFileForMakeSign(MD5_CTX* md5, const char* filename, const char* basePath);
const char* SmoothWeightsToString(float in, float out);
int32_t StringToBlendMode(const char* string, EGxBlend& blend);
int32_t StringToBOOL(const char* string);
int32_t StringToBOOL(lua_State* L, int32_t index, int32_t defaultValue);
int32_t StringToCurveType(const char* string, SimpleAnimCurveType& curve);
int32_t StringToDrawLayer(const char* string, uint32_t& layer);
uint32_t StringToFontFlags(const char* string);
int32_t StringToFramePoint(const char* string, FRAMEPOINT& point);
int32_t StringToFrameStrata(const char* string, FRAME_STRATA& strata);
int32_t StringToJustify(const char* string, uint32_t& justify);
int32_t StringToLoopType(const char* string, SimpleAnimLoopType& loop);
int32_t StringToOrientation(const char* string, ORIENTATION& orientation);
int32_t StringToSmoothWeights(const char* string, float& in, float& out);

#endif
