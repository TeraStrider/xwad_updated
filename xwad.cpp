//========= Copyright © 1996-2005, Valve Corporation, All rights reserved.
//============//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//

#include <windows.h>
#include <map>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <conio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <direct.h>
#include <stdarg.h>

#include "goldsrc_standin.h"

#include "wadlib.h"
#include "goldsrc_bspfile.h"


extern FILE *wadhandle;

#define max(a, b) a > b ? a : b
#pragma pack(1)
struct TGAHeader_t {
  unsigned char id_length;
  unsigned char colormap_type;
  unsigned char image_type;
  unsigned short colormap_index;
  unsigned short colormap_length;
  unsigned char colormap_size;
  unsigned short x_origin;
  unsigned short y_origin;
  unsigned short width;
  unsigned short height;
  unsigned char pixel_size;
  unsigned char attributes;
};
#pragma pack()

typedef enum { ST_SYNC = 0, ST_RAND } synctype_t;
typedef enum { SPR_SINGLE = 0, SPR_GROUP } spriteframetype_t;

typedef struct {
  int ident;
  int version;
  int type;
  int texFormat;
  float boundingradius;
  int width;
  int height;
  int numframes;
  float beamlength;
  synctype_t synctype;
} dsprite_t;

typedef struct {
  int origin[2];
  int width;
  int height;
} dspriteframe_t;

class RGBAColor {
 public:
  unsigned char r, g, b, a;
};

const char *g_pDefaultShader = "lightmappedgeneric";
const char *g_pShader = g_pDefaultShader;
bool g_bBMPAllowTranslucent = false;
bool g_bDecal = false;
bool g_bQuiet = false;

//vmtcmd additions
const char *g_pMaterialtxt = NULL;
#define VMT_TRANSPARENT   0b00000000000000000000000000000001
#define VMT_WATER         0b00000000000000000000000000000010
#define VMT_ANIMATED      0b00000000000000000000000000000100
#define VMT_TOGGLED       0b00000000000000000000000000001000
#define VMT_TILING        0b00000000000000000000000000010000
#define VMT_SKY           0b00000000000000000000000000100000
#define VMT_SCROLL        0b00000000000000000000000001000000
#define VMT_METAL         0b00000000000000000000000010000000
#define VMT_VENT          0b00000000000000000000000100000000
#define VMT_DIRT          0b00000000000000000000001000000000
#define VMT_SLOSH         0b00000000000000000000010000000000
#define VMT_TILE          0b00000000000000000000100000000000
#define VMT_GRATE         0b00000000000000000001000000000000
#define VMT_WOOD          0b00000000000000000010000000000000
#define VMT_COMPUTER      0b00000000000000000100000000000000
#define VMT_GLASS         0b00000000000000001000000000000000
#define VMT_FOGINTENSITY  0b00000000000000010000000000000000

struct VTexVMTParam_t {
  const char *m_szParam;
  const char *m_szValue;
};
#define MAX_VMT_PARAMS 16

static VTexVMTParam_t g_VMTParams[MAX_VMT_PARAMS];

static int g_NumVMTParams = 0;

//vmtcmd additions
#define MAX_VTF_PARAMS 16

static VTexVMTParam_t g_VTFParams[MAX_VTF_PARAMS];

static int g_NumVTFParams = 0;

void PrintExitStuff() {
  if (!g_bQuiet) {
    printf("Press a key to quit.\n");
    getch();
  }
}

RGBAColor *ConvertToRGBAUpsideDown(byte *pBits, int width, int height, byte *pPalette, bool *bAlphatest) {
  RGBAColor *pRet = new RGBAColor[width * height];

  // Write the lines upside-down.
  for (int y = 0; y < height; y++) {
    byte *pLine = &pBits[(height - y - 1) * width];
    for (int x = 0; x < width; x++) {
      if (g_bDecal) {
        pRet[y * width + x].r = pPalette[255 * 3 + 2];
        pRet[y * width + x].g = pPalette[255 * 3 + 1];
        pRet[y * width + x].b = pPalette[255 * 3 + 0];
        pRet[y * width + x].a = pLine[x];
      } else {
        pRet[y * width + x].r = pPalette[pLine[x] * 3 + 2];
        pRet[y * width + x].g = pPalette[pLine[x] * 3 + 1];
        pRet[y * width + x].b = pPalette[pLine[x] * 3 + 0];
        if (pLine[x] == 255) {
          *bAlphatest = true;
          pRet[y * width + x].a = 0;
        } else {
          pRet[y * width + x].a = 255;
        }     
      }
    }
  }

  return pRet;
}

// adds texture bleeding so that $laphatest does not break
void FloodSolidPixels(RGBAColor *pTexels, int width, int height) {
  byte *pAlphaMap = new byte[width * height];
  byte *pNewAlphaMap = new byte[width * height];

  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      pAlphaMap[y * width + x] = pTexels[y * width + x].a;

  bool bHappy = false;
  while (!bHappy) {
    bHappy = true;

    memcpy(pNewAlphaMap, pAlphaMap, width * height);

    for (int y = 0; y < height; y++) {
      RGBAColor *pLine = &pTexels[y * width];

      for (int x = 0; x < width; x++) {
        if (pAlphaMap[y * width + x] == 0) {
          int nNeighbors = 0;
          int neighborTotal[3] = {0, 0, 0};

          // Blend all the neighboring solid pixels.
          for (int offsetX = -1; offsetX <= 1; offsetX++) {
            for (int offsetY = -1; offsetY <= 1; offsetY++) {
              int testX = x + offsetX;
              int testY = y + offsetY;
              if (testX >= 0 && testY >= 0 && testX < width && testY < height) {
                if (pAlphaMap[testY * width + testX]) {
                  RGBAColor *pNeighbor = &pTexels[testY * width + testX];
                  ++nNeighbors;
                  neighborTotal[0] += pNeighbor->r;
                  neighborTotal[1] += pNeighbor->g;
                  neighborTotal[2] += pNeighbor->b;
                }
              }
            }
          }

          if (nNeighbors) {
            pNewAlphaMap[y * width + x] = 255;
            bHappy = false;
            pLine[x].r = (byte)(neighborTotal[0] / nNeighbors);
            pLine[x].g = (byte)(neighborTotal[1] / nNeighbors);
            pLine[x].b = (byte)(neighborTotal[2] / nNeighbors);
          }
        }
      }
    }

    memcpy(pAlphaMap, pNewAlphaMap, width * height);
  }

  delete[] pAlphaMap;
  delete[] pNewAlphaMap;
}

// RGBAColor *ResampleImage(RGBAColor *pRGB, int width, int height, int newWidth,
//                          int newHeight) {
//   RGBAColor *pResampled = new RGBAColor[newWidth * newHeight];
//   for (int y = 0; y < newHeight; y++) {
//     float yPercent = (float)y / (newHeight - 1);
//     float flSrcY = yPercent * (height - 1.00001f);
//     int iSrcY = (int)flSrcY;
//     float flYFrac = flSrcY - iSrcY;

//     for (int x = 0; x < newWidth; x++) {
//       float xPercent = (float)x / (newWidth - 1);
//       float flSrcX = xPercent * (width - 1.00001f);
//       int iSrcX = (int)flSrcX;
//       float flXFrac = flSrcX - iSrcX;

//       byte *pSrc0 = ((byte *)&pRGB[iSrcY * width + iSrcX]);
//       byte *pSrc1 = ((byte *)&pRGB[iSrcY * width + iSrcX + 1]);
//       byte *pSrc2 = ((byte *)&pRGB[(iSrcY + 1) * width + iSrcX]);
//       byte *pSrc3 = ((byte *)&pRGB[(iSrcY + 1) * width + iSrcX + 1]);
//       byte *pDest = (byte *)&pResampled[y * newWidth + x];

//       // Now blend the nearest 4 source pixels.
//       for (int i = 0; i < 4; i++) {
//         // pDest[i] = pSrc0[i];
//         float topColor = (pSrc0[i] * (1 - flXFrac) + pSrc1[i] * flXFrac);
//         float bottomColor = (pSrc2[i] * (1 - flXFrac) + pSrc3[i] * flXFrac);
//         pDest[i] = (byte)(topColor * (1 - flYFrac) + bottomColor * flYFrac);
//       }
//     }
//   }
//   return pResampled;
// }



bool WriteTGAFile(const char *pFilename, bool bAllowTranslucent, byte *pBits,
                  int width, int height, byte *pPalette, bool bPowerOf2,
                  bool *bAlphatest, bool *bResized) {
  *bResized = *bAlphatest = false;

  RGBAColor *pRGB = ConvertToRGBAUpsideDown(pBits, width, height, pPalette, bAlphatest);

  // Unless the filename starts with '{', we don't allow translucency.
  if (!bAllowTranslucent) *bAlphatest = false;

  if (*bAlphatest) {
      // Flood the solid texel colors into the transparent texels.
      // Since we turn on point sampling for these textures, this only matters if
      // we're resizing the texture.
      FloodSolidPixels(pRGB, width, height);
  }

  if (bPowerOf2) {
    // Is it not a power of 2?
    if ((width & (width - 1)) || (height & (height - 1))) {
      // Ok, resize it to the next highest power of 2.
      int newWidth = width;
      while ((newWidth & (newWidth - 1))) ++newWidth;

      int newHeight = height;
      while ((newHeight & (newHeight - 1))) ++newHeight;

      if (!g_bQuiet) printf("\t (%dx%d) -> (%dx%d)\n", width, height, newWidth, newHeight);

      //RGBAColor *pResampled =
          //ResampleImage(pRGB, width, height, newWidth, newHeight);
      //delete[] pRGB;
      //pRGB = pResampled;

      //width = newWidth;
      //height = newHeight;

      *bResized = true;
    }
  }

  // Write it..
  TGAHeader_t hdr;
  memset(&hdr, 0, sizeof(hdr));

  hdr.width = width;
  hdr.height = height;
  hdr.colormap_type = 0;  // no, no colormap please
  hdr.image_type = 2;     // uncompressed, true-color
  if (*bAlphatest || g_bDecal) {
    hdr.pixel_size = 32;    // 32 bits per pixel
  } else {
    hdr.pixel_size = 24;    // 32 bits per pixel
  }

  FILE *fp = fopen(pFilename, "wb");
  if (!fp) return false;

  SafeWrite(fp, &hdr, sizeof(hdr));
  if (*bAlphatest || g_bDecal) {
    SafeWrite(fp, pRGB, sizeof(RGBAColor) * width * height);
  } else {
    for (int i = 0; i < height * width; i++) {
      SafeWrite(fp, pRGB + i, sizeof(unsigned char) * 3);
    }
  }
  fclose(fp);

  delete[] pRGB;
  return true;
}

int PrintUsage(const char *pExtra) {
  printf(
      "%s \n"
      "\t[-autodir]\n"
      "\t\tautomatically detects -basedir and -wadfile or -bmpfile based\n"
      "\t\ton the last parameter (it must be a wad file or a bmp file.\n"
      "\t[-decal]\n"
      "\t\tcreates vmts for decals and creates vmts for model decals.\n"
      "\t[-onlytex <tex name>]\n"
      "\t[-shader <shader name>]\n"
      "\t\tspecify the shader to use in the vmt file (default\n"
      "\t\tis lightmappedgeneric.\n"
      "\t[-vtex]\n"
      "\t\tif -vtex is specified, then it calls vtex on each newly-created\n"
      "\t\t.tga file.(abandonware)\n"
      "\t[-vtfcmd <vtfcmd.exe path>]\n"
      "\t\tif vtfcmd is specified, then it calls vtfcmd on each\n"
      "\t\tnewly-created .tga file.\n"
      "\t[-materials <materials.txt path>]\n"
      "\t\tif materials is specified it will add appropriate surfaceproperties\n"
      "\t[-vmtparam <paramname> <paramvalue>]\n"
      "\t\tif -vtex was specified, passes the parameters to that process.\n"
      "\t\tused to add parameters to the generated .vmt file\n"
      "\t-basedir <basedir>\n"
      "\t-game <basedir>\n"
      "\t\tspecifies where the root mod directory is.\n"
      "\t-wadfile <wildcard>\n"
      "\t\t-wadfile will make (power-of-2) tgas, vtfs, and vmts for each\n"
      "\t\ttexture in the wad. it will also place them under a directory\n"
      "\t\twith the name of the wad. in addition, it will create\n"
      "\t\t.resizeinfo files in the materials directory if it has to\n"
      "\t\tresize the texture. then hammer's file->wad to material\n"
      "\t\tcommand will use them to rescale texture coordinates.\n"
      "\t-bmpfile <wildcard>\n"
      "\t\t-bmpfile acts like -wadfile but for bmp files, and it'll place\n"
      "\t\tthem in the root materials directory.\n"
      "\t-sprfile <wildcard>\n"
      "\t\tacts like -bmpfile, but ports a sprite.\n"
      "\t-transparent (bmp files only)\n"
      "\t\tif this is set, then it will treat palette index 255 as a\n"
      "\t\ttransparent pixel.\n"
      "\t-subdir <subdirectory>\n"
      "\t\t-subdir tells it what directory under materials to place the\n"
      "\t\tfinal art. if using a wad file, then it will automatically\n"
      "\t\tuse the wad filename if no -subdir is specified.\n"
      "\t-quiet\n"
      "\t\tdon't print out anything or wait for a keypress on exit.\n"
      "\n",
      pExtra);
  printf("ex: %s -vtex -basedir c:\\hl2\\dod -wadfile c:\\hl1\\dod\\*.wad\n",
         pExtra);
  printf(
      "ex: %s -vtex -basedir c:\\hl2\\dod -bmpfile test.bmp -subdir "
      "models\\props\n",
      pExtra);
  printf(
      "ex: %s -vtex -vmtparam $ignorez 1 -basedir c:\\hl2\\dod -sprfile "
      "test.spr -subdir sprites\\props\n",
      pExtra);

  PrintExitStuff();
  return 1;
}

// Take something like "c:/a/b/c/filename.ext" and return "filename".
void GetBaseFilename(const char *pWadFilename, char wadBaseName[512]) {
  const char *pBase = strrchr(pWadFilename, '\\');
  if (strrchr(pWadFilename, '/') > pBase) pBase = strrchr(pWadFilename, '/');

  if (pBase)
    strcpy(wadBaseName, pBase + 1);
  else
    strcpy(wadBaseName, pWadFilename);

  char *pDot = strchr(wadBaseName, '.');
  if (pDot) *pDot = 0;
}

const char *LastSlash(const char *pSrc) {
  const char *pRet = strrchr(pSrc, '/');
  const char *pRet2 = strrchr(pSrc, '\\');
  return (pRet > pRet2) ? pRet : pRet2;
}

void EnsureDirExists(const char *pDir) {
  if (_access(pDir, 0) != 0) {
    // We use the shell's "md" command here instead of the _mkdir() function
    // because
    // md will create all the subdirectories leading up to the bottom one and
    // _mkdir() won't.
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "md \"%s\"", pDir);
    system(cmd);

    if (_access(pDir, 0) != 0) Error("\tCan't create directory: %s.\n", pDir);
  }
}


char *FilenameParams(const char *pName, int *vmtparams) {
  int vmtp = 0;
  if (*pName == '{') {
    vmtp |= VMT_TRANSPARENT;
    pName++;
  } else if (*pName == '+' && *(pName + 1) <= '9' && *(pName + 1) >= '0') {
    vmtp |= VMT_ANIMATED;
    pName+=2;
  } else if (*pName == '+' && ((*(pName + 1) <= 'Z' && *(pName + 1) >= 'A') || (*(pName + 1) <= 'z' && *(pName + 1) >= 'a'))) {
    vmtp |= VMT_TOGGLED;
    pName+=2;
  } else if (*pName == '-' ) {
    vmtp |= VMT_TILING;
    pName+=2;
  } else if (*pName == '!') {
    vmtp |= VMT_WATER;
    pName++;
  } else if (strncmp(pName, "scroll", 6) == 0) {
    vmtp |= VMT_SCROLL;
    pName+=6;
  }
  if (*pName == '~') {
    pName++;
  }
  *vmtparams = vmtp;
  return (char *)pName;
}

void WriteVMTFile(const char *pBaseDir, const char *pSubDir, const char *pName,
                  bool bAlphatest, char fogintensity, int fogcolor, char **matkeys, char *matvals, int pairs) {
  char vmtFilename[512];
  sprintf(vmtFilename, "%s\\materials\\%s\\%s.vmt", pBaseDir, pSubDir, pName);

  FILE *fp = fopen(vmtFilename, "wt");
  if (!fp) {
    Error("\tWriteVMTFile failed to open %s for writing.\n", vmtFilename);
    return;
  }
  int vmtparams = 0;
  char *pCleanName = FilenameParams(pName, &vmtparams);

  fprintf(fp, "\"%s\"\n{\n", g_pShader);
  fprintf(fp, "\t\"$basetexture\"\t\"%s\\%s\"\n", pSubDir, pName);

  if (g_bDecal) {
    fprintf(fp, "\t\"$translucent\"\t\t\"1\"\n");
    fprintf(fp, "\t\"$decal\"\t\t\"1\"\n");
  } else if (vmtparams & VMT_TRANSPARENT) {
    fprintf(fp, "\t\"$alphatest\"\t\"1\"\n");
    fprintf(fp, "\t\"$alphatestreference\"\t\"0.5\"\n");
  } else if (vmtparams & VMT_WATER) {
    fprintf(fp, "\t\"%%compilewater\"\t\"1\"\n");
    fprintf(fp, "\t\"$bottommaterial\"\t\"%s\\%s\"\n", pSubDir, pName);
    fprintf(fp, "\t\"$fogenable\"\t\"1\"\n");
    fprintf(fp, "\t\"$fogstart\"\t\"0\"\n");
    fprintf(fp, "\t\"$fogend\"\t\"%d\"\n", (255 - fogintensity + 64));
    fprintf(fp, "\t\"$fogcolor\"\t\"{%d %d %d}\"\n", (fogcolor) & 255, (fogcolor >> 8) & 255, (fogcolor >> 16) & 255);
  }
  int i;
  char lastmat = 0;
  for (i = 0; i < pairs; i++) {
    if (strlen(matkeys[i]) < 12) {
      if (!stricmp(matkeys[i], pName)) {
        lastmat = matvals[i];
      } else if (!stricmp(matkeys[i], pCleanName)) {
        lastmat = matvals[i];
      }
    } else {
      if (!strnicmp(matkeys[i], pName, strlen(matkeys[i]))) {
        lastmat = matvals[i];
      }
    }
  }
  if (!g_bQuiet && lastmat) {
    printf("\t LastMaterial [%c]\n", lastmat);
  }
  if (lastmat == 'M') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"metal\"\n");
  } else if (lastmat == 'V') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"vent\"\n");
  } else if (lastmat == 'D') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"dirt\"\n");
  } else if (lastmat == 'S') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"water\"\n");
  } else if (lastmat == 'T') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"tile\"\n");
  } else if (lastmat == 'G') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"metalgrate\"\n");
  } else if (lastmat == 'W') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"wood\"\n");
  } else if (lastmat == 'P') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"computer\"\n");
  } else if (lastmat == 'Y') {
    fprintf(fp, "\t\"$surfaceprop\"\t\"glass\"\n");
  }
  for (i = 0; i < g_NumVMTParams; i++) {
    fprintf(fp, "\t\"%s\" \"%s\"\n", g_VMTParams[i].m_szParam,
            g_VMTParams[i].m_szValue);
  }

  fprintf(fp, "}");

  fclose(fp);
}

void WriteTXTFile(const char *pBaseDir, const char *pSubDir,
                  const char *pName) {
  char filename[512];
  sprintf(filename, "%s\\materialsrc\\%s\\%s.txt", pBaseDir, pSubDir, pName);

  FILE *fp = fopen(filename, "wt");
  if (!fp) Error("\tWriteTXTFile: can't open %s for writing.\n", filename);

  fprintf(fp, "\"pointsample\" \"1\"\n");
  fclose(fp);
}

void WriteResizeInfoFile(const char *pBaseDir, const char *pSubDir,
                         const char *pName, int width, int height) {
  char filename[512];
  sprintf(filename, "%s\\materials\\%s\\%s.resizeinfo", pBaseDir, pSubDir,
          pName);

  FILE *fp = fopen(filename, "wt");
  if (!fp) {
    Error("\tWriteResizeInfoFile failed to open %s for writing.\n", filename);
    return;
  }

  fprintf(fp, "%d %d", width, height);
  fclose(fp);
}

void RunVTexOnFile(const char *pBaseDir, const char *pFilename) {
  char executableDir[MAX_PATH];
  GetModuleFileName(NULL, executableDir, sizeof(executableDir));

  char *pLastSlash =
      max(strrchr(executableDir, '/'), strrchr(executableDir, '\\'));
  if (!pLastSlash) Error("Can't find filename in '%s'.\n", executableDir);

  *pLastSlash = 0;

  // Set the vproject environment variable (vtex doesn't allow game yet).
  char envStr[MAX_PATH];
  _snprintf(envStr, sizeof(envStr), "vproject=%s", pBaseDir);
  putenv(envStr);

  // Call vtex on this texture now.
  char vtexCommand[1024];
  sprintf(vtexCommand, "%s\\vtex.exe -quiet -nopause \"%s\"", executableDir,
          pFilename);
  if (system(vtexCommand) != 0) {
    Error("\tCommand '%s' failed!\n", vtexCommand);
  }
}

void RunVTFCMDOnFile(const char *pBaseDir, const char *pSubDir, const char *pName, const char *pFilename,
					 const char *pVTFcmdexe) {
  // Call vtfcmd on this texture now.
  char vtfcmdcommand[1024];
  sprintf(vtfcmdcommand, "\"\"%s\" -silent -resize -rmethod BIGGEST -file \"%s\" -output \"%s\\materials\\%s\"\"", pVTFcmdexe, pFilename, pBaseDir, pSubDir);
  if (system(vtfcmdcommand) != 0) {
    Error("\tCommand '%s' failed!\n", vtfcmdcommand);
  } else if (!g_bQuiet) {
  	printf("\t (%s) -> (%s.vtf)\n", pName, pName);
  }
}

void WriteOutputFiles(const char *pBaseDir, const char *pSubDir,
                      const char *pName, bool bAllowTranslucent, byte *buffer,
                      int width, int height, byte *pPalette, bool bVTex,
                      const char *pVTFcmdexe, char **matkeys, char *matvals, int pairs) {
  bool bAlphatest, bResized;
  bool bPowerOf2 = true;
  int  vmtparams = 0;
  char fogintensity = 0;
  int  fogcolor;
  fogcolor = (((int)pPalette[9]) | ((int)pPalette[10] << 8) | ((int)pPalette[11] << 16));
  if (pPalette[13] == 0 && pPalette[14] == 0) {
    fogintensity = pPalette[12];
  }
  char tgaFilename[1024];
  sprintf(tgaFilename, "%s\\materialsrc\\%s\\%s.tga", pBaseDir, pSubDir, pName);
  if (!WriteTGAFile(tgaFilename, bAllowTranslucent, buffer, width, height,
                    pPalette, bPowerOf2, &bAlphatest, &bResized)) {
    Error("\tError writing %s.\n", tgaFilename);
  }

  // Write its .VMT file.
  WriteVMTFile(pBaseDir, pSubDir, pName, bAlphatest, fogintensity, fogcolor, matkeys, matvals, pairs);

  // Write a text file for it if it's translucent so we can enable pointsample
  // for vtex.
  //	if ( bAlphatest )
  //		WriteTXTFile( pBaseDir, pSubDir, pName );

  // Write its .resizeinfo file.

  // if (bVTex) {
  //   RunVTexOnFile(pBaseDir, tgaFilename);
  // }
  if (pVTFcmdexe) {
  	RunVTFCMDOnFile(pBaseDir, pSubDir, pName, tgaFilename, pVTFcmdexe);
  }
  if (bResized) {
    WriteResizeInfoFile(pBaseDir, pSubDir, pName, width, height);
  }
}

void EnsureDirectoriesExist(const char *pBaseDir, const char *pSubDir) {
  char materialsrcDir[512], materialsDir[512];
  sprintf(materialsrcDir, "%s\\materialsrc\\%s", pBaseDir, pSubDir);
  sprintf(materialsDir, "%s\\materials\\%s", pBaseDir, pSubDir);
  EnsureDirExists(materialsrcDir);
  EnsureDirExists(materialsDir);
}

void ProcessWadFile(const char *pWadFilename, const char *pBaseDir,
                    const char *pSubDir, const char *pOnlyTex, bool bVTex,
                    const char *pVTFcmdexe, char **matkeys, char *matvals, int pairs) {
  if (!g_bQuiet) printf("\n\n[WADFILE %s]\n\n", pWadFilename);

  // If no -subdir was specified, then figure it out from the wad filename.
  char wadBaseName[512];
  if (!pSubDir) {
    // Get the base wad filename.
    GetBaseFilename(pWadFilename, wadBaseName);
    pSubDir = wadBaseName;
  }

  EnsureDirectoriesExist(pBaseDir, pSubDir);

  // Now process all the images in the wad.
  W_OpenWad(pWadFilename);

  #define MAXLUMP (640 * 480 * 85 / 64)
  byte inbuffer[MAXLUMP];

  for (int i = 0; i < numlumps; i++) {
    if (pOnlyTex && stricmp(pOnlyTex, lumpinfo[i].name) != 0) continue;

    fseek(wadhandle, lumpinfo[i].filepos, SEEK_SET);
    SafeRead(wadhandle, inbuffer, lumpinfo[i].size);

    miptex_t *qtex = (miptex_t *)inbuffer;
    int width = LittleLong(qtex->width);
    int height = LittleLong(qtex->height);

    if (width <= 0 || height <= 0 || width > 5000 || height > 5000) {
      if (!g_bQuiet)
        printf("\tskipping %s @ %d  size %d (not an image?)\n",
               lumpinfo[i].name, lumpinfo[i].filepos, lumpinfo[i].size);
      continue;
    } else {
      if (!g_bQuiet) printf("\t%s\n", lumpinfo[i].name);
    }

    byte *pPalette =
        inbuffer + LittleLong(qtex->offsets[3]) + width * height / 64 + 2;
    byte *psrc, *pdest;

    byte outbuffer[(640 + 320) * 480];

    // The old xwad	put the mipmaps in there too, but we don't want that now
    // (usually).
    // copy in 0 image
    psrc = inbuffer + LittleLong(qtex->offsets[0]);
    pdest = outbuffer;
    for (int t = 0; t < height; t++) {
      memcpy(pdest + t * width, psrc + t * width, width);
    }

    WriteOutputFiles(pBaseDir,              // base directory
                     pSubDir,               // subdir under materials
                     qtex->name,            // filename (w/o extension)
                     qtex->name[0] == '{',  // allow transparency?
                     outbuffer, width, height, pPalette, bVTex, pVTFcmdexe, matkeys, matvals, pairs);
    if (!g_bQuiet) printf("\n");
  }

}

void ProcessBMPFile(const char *pBaseDir, const char *pSubDir,  const char *pFilename, bool bVTex, const char *pVTFcmdexe, char **matkeys, char *matvals, int pairs) {
  if (!g_bQuiet) printf("[%s]\n", pFilename);

  if (!pSubDir) pSubDir = ".";

  // First make directories under materialsrc and materials if they don't exist.
  EnsureDirectoriesExist(pBaseDir, pSubDir);

  // Read in the 8-bit BMP file.
  FILE *fp = fopen(pFilename, "rb");
  if (!fp)
    Error("ProcessBMPFile( %s ) can't open the file for reading.\n", pFilename);

  BITMAPFILEHEADER bfh;
  BITMAPINFOHEADER bih;
  unsigned char pixelData[512 * 512];

  SafeRead(fp, &bfh, sizeof(bfh));
  SafeRead(fp, &bih, sizeof(bih));

  // Make sure it's an 8-bit one like we want.
  if (bih.biSize != sizeof(bih) || bih.biPlanes != 1 || bih.biBitCount != 8 ||
      bih.biCompression != BI_RGB || bih.biHeight < 0 ||
      bih.biWidth * bih.biHeight > sizeof(pixelData)) {
    Error("ProcessBMPFile( %s ) - invalid format.\n", pFilename);
  }

  int nColorsUsed = 256;
  if (bih.biClrUsed != 0) {
    nColorsUsed = bih.biClrUsed;
    if (nColorsUsed > 256)
      Error("ProcessBMPFile( %s ) - bih.biClrUsed = %d.\n", pFilename,
            bih.biClrUsed);
  }

  RGBQUAD quadPalette[256];
  SafeRead(fp, quadPalette, sizeof(quadPalette[0]) * nColorsUsed);

  // Usually, bfOffBits is the same place we are at now, but sometimes it's a
  // little different.
  fseek(fp, bfh.bfOffBits, SEEK_SET);

  // Now read the bitmap data.
  SafeRead(fp, pixelData, bih.biWidth * bih.biHeight);

  fclose(fp);

  // Convert the palette.
  byte palette[256][3];
  for (int i = 0; i < 256; i++) {
    palette[i][0] = quadPalette[i].rgbRed;
    palette[i][1] = quadPalette[i].rgbGreen;
    palette[i][2] = quadPalette[i].rgbBlue;
  }

  // Unflip the pixel data.
  for (int y = 0; y < bih.biHeight / 2; y++) {
    byte tempLine[1024];
    memcpy(tempLine, &pixelData[y * bih.biWidth], bih.biWidth);
    memcpy(&pixelData[y * bih.biWidth],
           &pixelData[(bih.biHeight - y - 1) * bih.biWidth], bih.biWidth);
    memcpy(&pixelData[(bih.biHeight - y - 1) * bih.biWidth], tempLine,
           bih.biWidth);
  }

  char baseFilename[512];
  GetBaseFilename(pFilename, baseFilename);

  // Save it out.
  WriteOutputFiles(pBaseDir,                // base directory
                   pSubDir,                 // subdir under materials
                   baseFilename,            // filename (w/o extension)
                   g_bBMPAllowTranslucent,  // allow transparency
                   pixelData, bih.biWidth, bih.biHeight, (byte *)palette,
                   bVTex, pVTFcmdexe, matkeys, matvals, pairs);
}

void ProcessSPRFile(const char *pBaseDir, const char *pSubDir,  const char *pFilename, bool bVTex) {
  if (!g_bQuiet) printf("[%s]\n", pFilename);

  if (!pSubDir) pSubDir = ".";

  char baseFilename[512];
  GetBaseFilename(pFilename, baseFilename);

  // First make directories under materialsrc and materials if they don't exist.
  EnsureDirectoriesExist(pBaseDir, pSubDir);

  // Read in the SPR file.
  FILE *fp = fopen(pFilename, "rb");
  if (!fp)
    Error("ProcessSPRFile( %s ) can't open the file for reading.\n", pFilename);

  dsprite_t header;
  SafeRead(fp, &header, sizeof(header));

  // Make sure it's a sprite file.
  if (((header.ident >> 0) & 0xFF) != 'I' ||
      ((header.ident >> 8) & 0xFF) != 'D' ||
      ((header.ident >> 16) & 0xFF) != 'S' ||
      ((header.ident >> 24) & 0xFF) != 'P') {
    Warning("WARNING: sprite %s is not a sprite file. Skipping.\n", pFilename);
    fclose(fp);
    return;
  }

  if (header.version != 2) {
    Warning("WARNING: sprite %s is not a version 2 sprite file. Skipping.\n",
            pFilename);
    fclose(fp);
    return;
  }

  // Read the palette.
  short cnt;
  byte palette[768];
  SafeRead(fp, &cnt, sizeof(cnt));
  SafeRead(fp, palette, sizeof(palette));

  // Read the frames.
  int i;
  for (i = 0; i < header.numframes; i++) {
    spriteframetype_t type;
    SafeRead(fp, &type, sizeof(type));
    if (type == SPR_SINGLE) {
      dspriteframe_t frame;
      SafeRead(fp, &frame, sizeof(frame));
      if (frame.width > 5000 || frame.height > 5000 || frame.width < 1 ||
          frame.height < 1) {
        Warning(
            "WARNING: sprite %s has an invalid frame size (%d x %d) for frame "
            "%d.\n",
            pFilename, frame.width, frame.height, i);
        fclose(fp);
        return;
      }

      byte *frameData = new byte[frame.width * frame.height];
      SafeRead(fp, frameData, frame.width * frame.height);

      Msg("\tFrame %d   ", i);

      // Write the TGA file for this frame.
      bool bAlphatest, bResized;
      char frameFilename[512];
      _snprintf(frameFilename, sizeof(frameFilename),
                "%s\\materialsrc\\%s\\%s%03d.tga", pBaseDir, pSubDir,
                baseFilename, i);
      if (!WriteTGAFile(frameFilename, g_bBMPAllowTranslucent, frameData,
                        frame.width, frame.height, palette,
                        true,  // allow power-of-2
                        &bAlphatest, &bResized)) {
        Error("\tError writing %s.\n", frameFilename);
      }

      if (!g_bQuiet) printf("\n");

      delete[] frameData;
    } else if (type == SPR_GROUP) {
      Error(
          "Sprite %s uses type SPR_GROUP. Get a programmer to add support for "
          "it.\n",
          pFilename);
    } else {
      Warning(
          "WARNING: sprite %s has an invalid frame type (%d) for frame %d.\n",
          pFilename, type, i);
      fclose(fp);
      return;
    }
  }

  fclose(fp);

  //
  // Generate a .txt file for the sprite.
  //
  char txtFilename[512];
  sprintf(txtFilename, "%s\\materialsrc\\%s\\%s.txt", pBaseDir, pSubDir,
          baseFilename);

  fp = fopen(txtFilename, "wt");
  if (!fp) Error("\tProcessSPRFile: can't open %s for writing.\n", txtFilename);

  fprintf(fp, "\"startframe\" \"0\"\n");
  fprintf(fp, "\"endframe\" \"%d\"\n", header.numframes - 1);
  fprintf(fp, "\"nomip\" \"1\"\n");
  fprintf(fp, "\"nolod\" \"1\"\n");
  fclose(fp);

  //
  // Run VTEX on the .txt file?
  //
  // if (bVTex) {
  //   RunVTexOnFile(pBaseDir, txtFilename);
  // }

  //
  // Generate a .vmt file.
  //
  char vmtFilename[512];
  _snprintf(vmtFilename, sizeof(vmtFilename), "%s\\materials\\%s\\%s.vmt",
            pBaseDir, pSubDir, baseFilename);
  fp = fopen(vmtFilename, "wt");
  if (!fp) Error("\tProcessSPRFile: can't open %s for writing.\n", vmtFilename);

  if (g_pShader == g_pDefaultShader)
    fprintf(fp, "\"UnlitGeneric\"\n");
  else
    fprintf(fp, "\"%s\"\n", g_pShader);

  fprintf(fp, "{\n");
  fprintf(fp, "\t\"$spriteorientation\" \"vp_parallel\"\n");
  fprintf(fp, "\t\"$spriteorigin\" \"[ 0.50 0.50 ]\"\n");
  fprintf(fp, "\t\"$basetexture\" \"%s/%s\"\n", pSubDir, baseFilename);

  for (i = 0; i < g_NumVMTParams; i++) {
    fprintf(fp, "\t\"%s\" \"%s\"\n", g_VMTParams[i].m_szParam,
            g_VMTParams[i].m_szValue);
  }

  fprintf(fp, "}");

  fclose(fp);
}

void ExtractDirectory(const char *pFilename, char *prefix) {
  const char *pSlash = strrchr(pFilename, '/');
  if (strrchr(pFilename, '\\') > pSlash) pSlash = strrchr(pFilename, '\\');

  if (pSlash) {
    memcpy(prefix, pFilename, pSlash - pFilename);
    prefix[pSlash - pFilename] = 0;
  } else {
    strcpy(prefix, ".\\");
  }
}

// This allows them to have a WAD or BMP under their materialsrc directory and
// it'll try to figure out
// all the other parameters for them.
bool DragAndDropCheck(const char **pBaseDir, const char **pSubDir,
                      const char **pWadFilenames, const char **pBMPFilenames,
                      const char **pSPRFilenames, bool *bVTex) {
  const char *pLastParam = __argv[__argc - 1];

  // Get the first argument in upper case.
  char arg1[512];
  strncpy(arg1, pLastParam, sizeof(arg1));
  strupr(arg1);

  // Only handle it if there's a full path (with a colon).
  if (!strchr(arg1, ':')) return false;

  if (strstr(arg1, ".WAD"))
    *pWadFilenames = pLastParam;
  else if (strstr(arg1, ".BMP"))
    *pBMPFilenames = pLastParam;
  else if (strstr(arg1, ".SPR"))
    *pSPRFilenames = pLastParam;
  else
    return false;

  // Ok, we know that argv[1] has a valid filename. Is it under materialsrc?
  char *pMatSrc = strstr(arg1, "MATERIALSRC");
  if (!pMatSrc || pMatSrc == arg1) return false;

  // The base directory is everything before materialsrc.
  static char baseDir[512];
  *pBaseDir = baseDir;
  memcpy(baseDir, arg1, pMatSrc - arg1);
  baseDir[pMatSrc - arg1 - 1] = 0;

  // The subdirectory is everything after materialsrc, minus the filename.
  char *pSubDirSrc = pMatSrc + strlen("MATERIALSRC") + 1;
  char *pEnd = strrchr(pSubDirSrc, '\\');
  if (strrchr(pSubDirSrc, '/') > pEnd) pEnd = strrchr(pSubDirSrc, '/');

  if (!pEnd) return false;

  static char subDir[512];
  *pSubDir = subDir;
  memcpy(subDir, pSubDirSrc, pEnd - pSubDirSrc);
  subDir[pEnd - pSubDirSrc] = 0;

  // Always use vtex in drag-and-drop mode.
  *bVTex = true;
  return true;
}

void ParseMaterial(const char *g_pMaterialtxt, char ***key, char **value, int *pairs) {
  FILE *fp = fopen(g_pMaterialtxt, "r");
  if (!fp) {
    if (!g_bQuiet) printf("\nCould not Open %s\n", g_pMaterialtxt);
    return;
  }
  char line[125];
  int cap = 1;
  char **aux;
  char *auxs, *tok;
  *pairs = 0;
  while (fgets(line, 125, fp)) {
    if (*line != '/' && *line != ' ' && *line != '\n') {
      if (*pairs == 0) {
        *key = (char **)malloc(sizeof(char *));
        *value = (char *)malloc(sizeof(char));
      } else if (*pairs >= cap) {
        cap *= 2;
        aux = (char **)realloc(*key,cap * sizeof(char *));
        if (aux) *key = aux;
        auxs = (char *)realloc(*value, cap * sizeof(char));
        if (auxs) *value = auxs;
      }
      tok = strtok(line, " \n");
      (*value)[*pairs] = *tok;
      tok = strtok(NULL, " \n");
      (*key)[*pairs] = strdup(tok);
      (*pairs)++;
    }
  }
  for (int i = 0; i < *pairs; i++) {
    printf("%s %c\n", (*key)[i], (*value)[i]);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    return PrintUsage(argv[0]);
  }

  bool bWriteTGA = true;
  bool bWriteBMP = false;
  bool bPowerOf2 = true;
  bool bAutoDir = false;

  bool bVTex = false;
  const char *pBaseDir = NULL;
  const char *pWadFilenames = NULL;
  const char *pBMPFilenames = NULL;
  const char *pSPRFilenames = NULL;
  const char *pSubDir = NULL;
  const char *pOnlyTex = NULL;
  // support for vtfcmd
  const char *pVTFcmdexe = NULL;

  // Scan for options.
  for (int i = 1; i < argc; i++) {
    if ((i + 2) < argc) {
      if (stricmp(argv[i], "-vmtparam") == 0 &&
          g_NumVMTParams < MAX_VMT_PARAMS) {
        g_VMTParams[g_NumVMTParams].m_szParam = argv[i + 1];
        g_VMTParams[g_NumVMTParams].m_szValue = argv[i + 2];

        if (!g_bQuiet) {
          fprintf(stderr, "Adding .vmt parameter: \"%s\"\t\"%s\"\n",
                  g_VMTParams[g_NumVMTParams].m_szParam,
                  g_VMTParams[g_NumVMTParams].m_szValue);
        }

        g_NumVMTParams++;

        i += 2;
      } else if (stricmp(argv[i], "-vtvparam") == 0 &&
          g_NumVMTParams < MAX_VMT_PARAMS) {
        g_VMTParams[g_NumVMTParams].m_szParam = argv[i + 1];
        g_VMTParams[g_NumVMTParams].m_szValue = argv[i + 2];

        if (!g_bQuiet) {
          fprintf(stderr, "Adding .vmt parameter: \"%s\"\t\"%s\"\n",
                  g_VMTParams[g_NumVMTParams].m_szParam,
                  g_VMTParams[g_NumVMTParams].m_szValue);
        }

        g_NumVMTParams++;

        i += 2;
      }
    }

    if ((i + 1) < argc) {
      if (stricmp(argv[i], "-basedir") == 0) {
        pBaseDir = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-wadfile") == 0) {
        pWadFilenames = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-bmpfile") == 0) {
        pBMPFilenames = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-sprfile") == 0) {
        pSPRFilenames = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-onlytex") == 0) {
        pOnlyTex = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-subdir") == 0) {
        pSubDir = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-shader") == 0) {
        g_pShader = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-vtfcmd") == 0) {
        pVTFcmdexe = argv[i + 1];
        ++i;
      } else if (stricmp(argv[i], "-materials") == 0) {
        g_pMaterialtxt = argv[i + 1];
        ++i;
      }
    }

    if (stricmp(argv[i], "-autodir") == 0) {
      bAutoDir = true;
    } else if (stricmp(argv[i], "-transparent") == 0) {
      g_bBMPAllowTranslucent = true;
    } else if (stricmp(argv[i], "-decal") == 0) {
      g_bDecal = true;
      //if (g_pShader == g_pDefaultShader) g_pShader = "decalmodulate";
    } else if (stricmp(argv[i], "-quiet") == 0) {
      g_bQuiet = true;
    } else if (stricmp(argv[i], "-vtex") == 0) {
      bVTex = true;
    }
  }

  if (bAutoDir) {
    if (!DragAndDropCheck(&pBaseDir, &pSubDir, &pWadFilenames, &pBMPFilenames,
                          &pSPRFilenames, &bVTex)) {
      printf("-AutoDir failed to setup directories.");
      return PrintUsage(argv[0]);
    }
  }

  if (!pBaseDir || (!pWadFilenames && !pBMPFilenames && !pSPRFilenames)) {
    printf("Missing a parameter.\n");
    return PrintUsage(argv[0]);
  }

  char **matkeys = NULL;
  char *matvals = NULL;
  int pairs = 0;

  if (g_pMaterialtxt != NULL) {
    ParseMaterial(g_pMaterialtxt, &matkeys, &matvals, &pairs);
  }

  char prefix[512];
  // Scan through each wadfile.
  if (pWadFilenames) {
    ExtractDirectory(pWadFilenames, prefix);

    _finddata_t findData;
    long handle = _findfirst(pWadFilenames, &findData);
    if (handle != -1) {
      do {
        if (!(findData.attrib & _A_SUBDIR)) {
          char fullFilename[512];
          sprintf(fullFilename, "%s\\%s", prefix, findData.name);
          ProcessWadFile(fullFilename, pBaseDir, pSubDir, pOnlyTex, bVTex, pVTFcmdexe, matkeys, matvals, pairs);
        }
      } while (_findnext(handle, &findData) == 0);

      _findclose(handle);
    }
  }

  // Process BMP files.
  if (pBMPFilenames) {
    ExtractDirectory(pBMPFilenames, prefix);

    _finddata_t findData;
    long handle = _findfirst(pBMPFilenames, &findData);
    if (handle != -1) {
      do {
        if (!(findData.attrib & _A_SUBDIR)) {
          char fullFilename[512];
          sprintf(fullFilename, "%s\\%s", prefix, findData.name);
          ProcessBMPFile(pBaseDir, pSubDir, fullFilename, bVTex, pVTFcmdexe, matkeys, matvals, pairs);
        }
      } while (_findnext(handle, &findData) == 0);

      _findclose(handle);
    }
  }

  if (pSPRFilenames) {
    ExtractDirectory(pSPRFilenames, prefix);

    _finddata_t findData;
    long handle = _findfirst(pSPRFilenames, &findData);
    if (handle != -1) {
      do {
        if (!(findData.attrib & _A_SUBDIR)) {
          char fullFilename[512];
          sprintf(fullFilename, "%s\\%s", prefix, findData.name);
          ProcessSPRFile(pBaseDir, pSubDir, fullFilename, bVTex);
        }
      } while (_findnext(handle, &findData) == 0);

      _findclose(handle);
    }
  }

  PrintExitStuff();
  return 0;
}
