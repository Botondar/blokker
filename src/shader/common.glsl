#define ATTRIB_POS 0
#define ATTRIB_TEXCOORD 1
#define ATTRIB_COLOR 2
#define ATTRIB_CHUNK_P 3

#if defined(VERTEX_SHADER)
#define vs_out out
#elif defined(FRAGMENT_SHADER)
#define vs_out in
#endif
