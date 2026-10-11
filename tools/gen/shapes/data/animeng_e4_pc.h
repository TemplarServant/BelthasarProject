/* SHAPE of a generated file (tools/gen/shapes.py): declarations and sizes only, no data.
   tools/setup.py replaces it with the file generated from your ROM. */
#ifndef CT_ANIMENG_E4_PC_H
#define CT_ANIMENG_E4_PC_H
#include <stdint.h>
#define CT_ANIMENG_CRONO_FOOT_X 134
#define CT_ANIMENG_CRONO_FOOT_Y 144   
#define CT_ANIMENG_CRONO_ATK_FOOT_Y 128 
#define CT_ANIMENG_MARLE_FOOT_X 176
#define CT_ANIMENG_MARLE_FOOT_Y 111   
static const uint8_t ctAnimengCronoIdleByState[3] = {0};
#define CT_ANIMENG_CRONO_STANCE 0xC6   
static const uint8_t ctAnimengCronoAtkByPose[6] = {0};
#define CT_ANIMENG_CRONO_ATK_STRIKE2 0x52  
typedef struct { uint8_t relStart, idx; } CtAnimengCritStep;
#define CT_ANIMENG_CRONO_CRIT_N 10
static const CtAnimengCritStep ctAnimengCronoCrit[CT_ANIMENG_CRONO_CRIT_N] = {0};
#define CT_ANIMENG_MARLE_STANCE 0xDA
#define CT_ANIMENG_MARLE_FLINCH 0x5B
static const uint8_t ctAnimengMarleAtkByPose[4] = {0};  
#define CT_ANIMENG_LUCCA_STANCE 0x31       
#define CT_ANIMENG_LUCCA_STANCE_ALT 0x39   
#define CT_ANIMENG_LUCCA_ATK_N 8
static const uint8_t ctAnimengLuccaAtkLadder[CT_ANIMENG_LUCCA_ATK_N] = {0};
#define CT_ANIMENG_FROG_STANCE 0x3B
#endif
