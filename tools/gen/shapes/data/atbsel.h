/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_ATBSEL_H
#define CT_ATBSEL_H
#include <stdint.h>
#define CT_ATBSEL_W 256
#define CT_ATBSEL_HGT 224
#define CT_ATBSEL_NPAL 82
static const uint8_t ctAtbSelPal[CT_ATBSEL_NPAL][3] = {0};
static const uint8_t ctAtbSelFrame0[CT_ATBSEL_HGT][CT_ATBSEL_W] = {0};
#define CT_ATBSEL_PATCH_X 40
#define CT_ATBSEL_PATCH_Y 93
#define CT_ATBSEL_PATCH_W 168
#define CT_ATBSEL_PATCH_H 81
static const uint8_t ctAtbSelPatch1[CT_ATBSEL_PATCH_H][CT_ATBSEL_PATCH_W] = {0};
#endif 
#define CT_ATBSEL_TEXT_X 32
#define CT_ATBSEL_NLINES 2
#define CT_ATBSEL_MAXSTEP 15
static const uint8_t ctAtbSelPenN[2][CT_ATBSEL_NLINES] = {0};
static const uint8_t ctAtbSelPen[2][CT_ATBSEL_NLINES][CT_ATBSEL_MAXSTEP] = {0};
#define CT_ATBSEL_EMPTY_X 40
#define CT_ATBSEL_EMPTY_Y 93
#define CT_ATBSEL_EMPTY_W 168
#define CT_ATBSEL_EMPTY_H 81
static const uint8_t ctAtbSelEmpty[CT_ATBSEL_EMPTY_H][CT_ATBSEL_EMPTY_W] = {0};
