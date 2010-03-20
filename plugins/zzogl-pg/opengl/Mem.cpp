/*  ZeroGS KOSMOS
 *  Copyright (C) 2005-2006 zerofrog@gmail.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "GS.h"
#include "Mem.h"
#include "zerogs.h"
#include "targets.h"
#include "x86.h"

#include "Mem_Transmit.h"
#include "Mem_Swizzle.h"

BLOCK m_Blocks[0x40]; // do so blocks are indexable

PCSX2_ALIGNED16(u32 tempblock[64]);

// ------------------------
// |              Y       |
// ------------------------
// |        block     |   |
// |   aligned area   | X |
// |                  |   |
// ------------------------
// |              Y       |
// ------------------------
#define DEFINE_TRANSFERLOCAL(psm, T, widthlimit, blockbits, blockwidth, blockheight, TransSfx, SwizzleBlock) \
int TransferHostLocal##psm(const void* pbyMem, u32 nQWordSize) \
{ \
	assert( gs.imageTransfer == 0 ); \
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; \
	\
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ \
	int i = gs.imageY, j = gs.imageX; \
	\
	const T* pbuf = (const T*)pbyMem; \
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch##TransSfx<T>(2)); \
	int nSize = nQWordSize*4*2/TransmitPitch##TransSfx<T>(2); \
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); \
	\
	int pitch, area, fracX; \
	int endY = ROUND_UPPOW2(i, blockheight); \
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); \
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); \
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; \
	\
	if( (gs.imageEndX-gs.trxpos.dx)%widthlimit ) { \
		/* hack */ \
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); \
		if((testwidth <= widthlimit) && (testwidth >= -widthlimit)) { \
			/* don't transfer */ \
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ \
			gs.imageTransfer = -1; \
		} \
		bCanAlign = false; \
	} \
	\
	/* first align on block boundary */ \
	if( MOD_POW2(i, blockheight) || !bCanAlign ) { \
		\
		if( !bCanAlign ) \
			endY = gs.imageEndY; /* transfer the whole image */ \
		else \
			assert( endY < gs.imageEndY); /* part of alignment condition */ \
		\
		if( ((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit) ) { \
			/* transmit with a width of 1 */ \
			TRANSMIT_HOSTLOCAL_Y(TransSfx,psm, T, (1+(DSTPSM == 0x14)), endY); \
		} \
		else { \
			TRANSMIT_HOSTLOCAL_Y(TransSfx,psm, T, widthlimit, endY); \
		} \
		\
		if( nSize == 0 || i == gs.imageEndY ) \
			goto End; \
	} \
	\
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); \
	\
	/* can align! */ \
	pitch = gs.imageEndX-gs.trxpos.dx; \
	area = pitch*blockheight; \
	fracX = gs.imageEndX-alignedX; \
	\
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ \
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch##TransSfx<T>(pitch) & 0xf) == 0; \
	\
	/* transfer aligning to blocks */ \
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) { \
		\
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) { \
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch##TransSfx<T>(blockwidth)/sizeof(T)) { \
				SwizzleBlock(pstart + getPixelAddress_0(psm,tempj, i, gs.dstbuf.bw)*blockbits/8, \
					(u8*)pbuf, TransmitPitch##TransSfx<T>(pitch)); \
			} \
		} \
		else { \
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch##TransSfx<T>(blockwidth)/sizeof(T)) { \
				SwizzleBlock##u(pstart + getPixelAddress_0(psm,tempj, i, gs.dstbuf.bw)*blockbits/8, \
					(u8*)pbuf, TransmitPitch##TransSfx<T>(pitch)); \
			} \
		} \
		\
		/* transfer the rest */ \
		if( alignedX < gs.imageEndX ) { \
			TRANSMIT_HOSTLOCAL_X(TransSfx,psm, T, widthlimit, blockheight, alignedX); \
			pbuf -= TransmitPitch##TransSfx<T>(alignedX-gs.trxpos.dx)/sizeof(T); \
		} \
		else pbuf += (blockheight-1)*TransmitPitch##TransSfx<T>(pitch)/sizeof(T); \
		j = gs.trxpos.dx; \
	} \
	\
	if( TransmitPitch##TransSfx<T>(nSize)/4 > 0 ) { \
		TRANSMIT_HOSTLOCAL_Y(TransSfx,psm, T, widthlimit, gs.imageEndY); \
		/* sometimes wrong sizes are sent (tekken tag) */ \
		assert( gs.imageTransfer == -1 || TransmitPitch##TransSfx<T>(nSize)/4 <= 2 ); \
	} \
	\
End: \
	if( i >= gs.imageEndY ) { \
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); \
		gs.imageTransfer = -1; \
		/*int start, end; \
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); \
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ \
	} \
	else { \
		/* update new params */ \
		gs.imageY = i; \
		gs.imageX = j; \
	} \
	return (nSize * TransmitPitch##TransSfx<T>(2) + nLeftOver)/2; \
} \

//#define NEW_TRANSFER
#ifdef NEW_TRANSFER

//DEFINE_TRANSFERLOCAL(32, u32, 2, 32, 8, 8, _, SwizzleBlock32);
int TransferHostLocal32(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 2;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u32);
//	_SwizzleBlock swizzle;

	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u32* pbuf = (const u32*)pbyMem; 
	const int tp2 = TransmitPitch_<u32>(2);
	int nLeftOver = (nQWordSize*4*2)%tp2; 
	int nSize = (nQWordSize*4*2)/tp2; 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(32, u32, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(32, u32, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u32>(pitch) & 0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		
		if ( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL))) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u32>(blockwidth)/TSize) 
			{ 
				u8 *temp = pstart + getPixelAddress_0(32, tempj, i, gs.dstbuf.bw)*blockbits/8;
				SwizzleBlock32(temp, (u8*)pbuf, TransmitPitch_<u32>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u32>(blockwidth)/TSize) 
			{ 
				u8 *temp = pstart + getPixelAddress_0(32, tempj, i, gs.dstbuf.bw)*blockbits/8;
				SwizzleBlock32u(temp, (u8*)pbuf, TransmitPitch_<u32>(pitch)); 
			} 
		} 
			
		
//		if ( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL))) 
//		{ 
//			swizzle = SwizzleBlock32;
//		} 
//		else 
//		{ 
//			swizzle = SwizzleBlock32u;
//		} 
//			
//		for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u32>(blockwidth)/TSize) 
//		{ 
//				u8 *temp = pstart + getPixelAddress_0(32, tempj, i, gs.dstbuf.bw)*blockbits/8;
//				swizzle(temp, (u8*)pbuf, TransmitPitch_<u32>(pitch), 0xffffffff); 
//		} 
		
		/* transfer the rest */ 
		if( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(32, u32, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u32>((alignedX-gs.trxpos.dx))/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u32>(pitch)/TSize; 
		}
		
		j = gs.trxpos.dx; 
	} 
	
	if ( TransmitPitch_<u32>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(32, u32, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u32>(nSize)/4 <= 2 ); 
	} 
	
End: 
	if( i >= gs.imageEndY ) 
	{ 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else 
	{ 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	
	return (nSize * TransmitPitch_<u32>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(32Z, u32, 2, 32, 8, 8, _, SwizzleBlock32);
int TransferHostLocal32Z(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 2;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u32);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u32* pbuf = (const u32*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u32>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u32>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(32Z, u32, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(32Z, u32, widthlimit, endY); 
		} 
		
		if ( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u32>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u32>(blockwidth)/sizeof(u32)) 
			{ 
				SwizzleBlock32(pstart + getPixelAddress_0(32Z,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u32>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u32>(blockwidth)/sizeof(u32)) 
			{ 
				SwizzleBlock32u(pstart + getPixelAddress_0(32Z,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u32>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_( 32Z, u32, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u32>(alignedX - gs.trxpos.dx)/sizeof(u32); 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u32>(pitch)/sizeof(u32);
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_<u32>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_( 32Z, u32, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u32>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u32>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(24, u8, 8, 32, 8, 8, _24, SwizzleBlock24);
int TransferHostLocal24(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 8;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_24<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_24<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_24(24, T, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_24(24, T, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_24<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_24<u8>(blockwidth)/sizeof(u8)) 
			{ 
				SwizzleBlock24(pstart + getPixelAddress_0(24,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_24<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_24<u8>(blockwidth)/sizeof(u8)) 
			{ 
				SwizzleBlock24u(pstart + getPixelAddress_0(24,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_24<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_24(24, T, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_24<u8>((alignedX-gs.trxpos.dx))/sizeof(u8); 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_24<u8>(pitch)/sizeof(u8);
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_24<u8>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_24(24, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_24<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_24<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(24Z, u8, 8, 32, 8, 8, _24, SwizzleBlock24);
int TransferHostLocal24Z(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 8;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_24<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_24<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_24(16, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_24(16, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_24<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_24<u8>(blockwidth)/sizeof(u8)) 
			{ 
				SwizzleBlock24(pstart + getPixelAddress_0(16,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_24<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_24<u8>(blockwidth)/sizeof(u8)) 
			{ 
				SwizzleBlock24u(pstart + getPixelAddress_0(16,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_24<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_24(16, u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_24<u8>(alignedX-gs.trxpos.dx)/sizeof(u8); 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_24<u8>(pitch)/sizeof(u8);
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_24<u8>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_24(24, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_24<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_24<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(16, u16, 4, 16, 16, 8, _, SwizzleBlock16);
int TransferHostLocal16(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 16;
	const u32 blockwidth = 16;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u16);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u16* pbuf = (const u16*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u16>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u16>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(16, u16, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(16, u16, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u16>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/sizeof(u16)) 
			{ 
				SwizzleBlock16(pstart + getPixelAddress_0(16,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/sizeof(u16)) 
			{ 
				SwizzleBlock16u(pstart + getPixelAddress_0(16,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(16, T, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u16>((alignedX-gs.trxpos.dx))/sizeof(u16); 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u16>(pitch)/sizeof(u16);
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_<u16>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(16, u16, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u16>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u16>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(16S, u16, 4, 16, 16, 8, _, SwizzleBlock16);
int TransferHostLocal16S(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 16;
	const u32 blockwidth = 16;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u16);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u16* pbuf = (const u16*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u16>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u16>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(16S, u16, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(16S, u16, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u16>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16(pstart + getPixelAddress_0(16S,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16u(pstart + getPixelAddress_0(16S,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(16S, u16, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u16>((alignedX-gs.trxpos.dx))/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u16>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_<u16>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(16S, u16, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u16>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u16>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(16Z, u16, 4, 16, 16, 8, _, SwizzleBlock16);
int TransferHostLocal16Z(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 16;
	const u32 blockwidth = 16;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u16);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u16* pbuf = (const u16*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u16>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u16>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(16Z, u16, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(16Z, u16, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u16>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16(pstart + getPixelAddress_0(16Z,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16u(pstart + getPixelAddress_0(16Z,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(16Z, T, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u16>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u16>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_<u16>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(16Z, u16, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u16>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u16>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(16SZ, u16, 4, 16, 16, 8, _, SwizzleBlock16);
int TransferHostLocal16SZ(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 16;
	const u32 blockwidth = 16;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u16);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u16* pbuf = (const u16*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u16>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u16>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(16SZ, u16, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(16SZ, u16, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u16>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16(pstart + getPixelAddress_0(16SZ,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u16>(blockwidth)/TSize) 
			{ 
				SwizzleBlock16u(pstart + getPixelAddress_0(16SZ,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u16>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(16SZ, u16, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u16>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u16>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_<u16>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(16SZ, u16, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u16>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u16>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(8, u8, 4, 8, 16, 16, _, SwizzleBlock8);
int TransferHostLocal8(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 8;
	const u32 blockwidth = 16;
	const u32 blockheight = 16;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(8, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(8, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY ) goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock8(pstart + getPixelAddress_0(8,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock8u(pstart + getPixelAddress_0(8,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(8, u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u8>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u8>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TRANSMIT_PITCH_(nSize, u8)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(8, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(4, u8, 8, 4, 32, 16, _4, SwizzleBlock4);
int TransferHostLocal4(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 8;
	const u32 blockbits = 4;
	const u32 blockwidth = 32;
	const u32 blockheight = 16;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_4<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_4<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_4(4, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_4(4, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY )  goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_4<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4(pstart + getPixelAddress_0(4,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4u(pstart + getPixelAddress_0(4,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_4(4, u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_4<u8>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_4<u8>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_4<u8>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_4(4, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_4<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_4<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(8H, u8, 4, 32, 8, 8, _, SwizzleBlock8H);
int TransferHostLocal8H(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 4;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_(8H, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_(8H, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY )  goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock8H(pstart + getPixelAddress_0(8H,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock8Hu(pstart + getPixelAddress_0(8H,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_(8H, u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_<u8>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_<u8>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TRANSMIT_PITCH_(nSize, u8)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_(8H, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(4HL, u8, 8, 32, 8, 8, _4, SwizzleBlock4HL);
int TransferHostLocal4HL(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 8;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_4<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_4<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_4(4HL, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_4(4HL, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY )  goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_4<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4HL(pstart + getPixelAddress_0(4HL,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4HLu(pstart + getPixelAddress_0(4HL,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_4(4HL, u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_4<u8>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_4<u8>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_4<u8>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_4(4HL, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_4<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_4<u8>(2) + nLeftOver)/2; 
} 

//DEFINE_TRANSFERLOCAL(4HH, u8, 8, 32, 8, 8, _4, SwizzleBlock4HH);
int TransferHostLocal4HH(const void* pbyMem, u32 nQWordSize) 
{ 
	const u32 widthlimit = 8;
	const u32 blockbits = 32;
	const u32 blockwidth = 8;
	const u32 blockheight = 8;
	const u32 TSize = sizeof(u8);
	
	assert( gs.imageTransfer == 0 ); 
	u8* pstart = g_pbyGSMemory + gs.dstbuf.bp*256; 
	
	/*const u8* pendbuf = (const u8*)pbyMem + nQWordSize*4;*/ 
	int i = gs.imageY, j = gs.imageX; 
	
	const u8* pbuf = (const u8*)pbyMem; 
	int nLeftOver = (nQWordSize*4*2)%(TransmitPitch_4<u8>(2)); 
	int nSize = nQWordSize*4*2/TransmitPitch_4<u8>(2); 
	nSize = min(nSize, gs.imageWnew * gs.imageHnew); 
	
	int pitch, area, fracX; 
	int endY = ROUND_UPPOW2(i, blockheight); 
	int alignedY = ROUND_DOWNPOW2(gs.imageEndY, blockheight); 
	int alignedX = ROUND_DOWNPOW2(gs.imageEndX, blockwidth); 
	bool bAligned, bCanAlign = MOD_POW2(gs.trxpos.dx, blockwidth) == 0 && (j == gs.trxpos.dx) && (alignedY > endY) && alignedX > gs.trxpos.dx; 
	
	if ((gs.imageEndX-gs.trxpos.dx)%widthlimit) 
	{ 
		/* hack */ 
		int testwidth = (int)nSize - (gs.imageEndY-i)*(gs.imageEndX-gs.trxpos.dx)+(j-gs.trxpos.dx); 
		if ((testwidth <= widthlimit) && (testwidth >= -widthlimit)) 
		{ 
			/* don't transfer */ 
			/*DEBUG_LOG("bad texture %s: %d %d %d\n", #psm, gs.trxpos.dx, gs.imageEndX, nQWordSize);*/ 
			gs.imageTransfer = -1; 
		} 
		bCanAlign = false; 
	} 
	
	/* first align on block boundary */ 
	if ( MOD_POW2(i, blockheight) || !bCanAlign ) 
	{ 
		
		if ( !bCanAlign ) 
			endY = gs.imageEndY; /* transfer the whole image */ 
		else 
			assert( endY < gs.imageEndY); /* part of alignment condition */ 
		
		if (((gs.imageEndX-gs.trxpos.dx)%widthlimit) || ((gs.imageEndX-j)%widthlimit)) 
		{ 
			/* transmit with a width of 1 */ 
			TRANSMIT_HOSTLOCAL_Y_4(4HH, u8, (1+(DSTPSM == 0x14)), endY); 
		} 
		else 
		{ 
			TRANSMIT_HOSTLOCAL_Y_4(4HH, u8, widthlimit, endY); 
		} 
		
		if( nSize == 0 || i == gs.imageEndY )  goto End; 
	} 
	
	assert( MOD_POW2(i, blockheight) == 0 && j == gs.trxpos.dx); 
	
	/* can align! */ 
	pitch = gs.imageEndX-gs.trxpos.dx; 
	area = pitch*blockheight; 
	fracX = gs.imageEndX-alignedX; 
	
	/* on top of checking whether pbuf is aligned, make sure that the width is at least aligned to its limits (due to bugs in pcsx2) */ 
	bAligned = !((uptr)pbuf & 0xf) && (TransmitPitch_4<u8>(pitch)&0xf) == 0; 
	
	/* transfer aligning to blocks */ 
	for(; i < alignedY && nSize >= area; i += blockheight, nSize -= area) 
	{ 
		if( bAligned || ((DSTPSM==PSMCT24) || (DSTPSM==PSMT8H) || (DSTPSM==PSMT4HH) || (DSTPSM==PSMT4HL)) ) 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4HH(pstart + getPixelAddress_0(4HH,tempj, i, gs.dstbuf.bw)*blockbits/8,  (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		else 
		{ 
			for(int tempj = gs.trxpos.dx; tempj < alignedX; tempj += blockwidth, pbuf += TransmitPitch_4<u8>(blockwidth)/TSize) 
			{ 
				SwizzleBlock4HHu(pstart + getPixelAddress_0(4HH,tempj, i, gs.dstbuf.bw)*blockbits/8, (u8*)pbuf, TransmitPitch_4<u8>(pitch)); 
			} 
		} 
		
		/* transfer the rest */ 
		if ( alignedX < gs.imageEndX ) 
		{ 
			TRANSMIT_HOSTLOCAL_X_4(4HH,u8, widthlimit, blockheight, alignedX); 
			pbuf -= TransmitPitch_4<u8>(alignedX-gs.trxpos.dx)/TSize; 
		} 
		else 
		{
			pbuf += (blockheight-1)*TransmitPitch_4<u8>(pitch)/TSize;
		}
		j = gs.trxpos.dx; 
	} 
	
	if (TransmitPitch_4<u8>(nSize)/4 > 0 ) 
	{ 
		TRANSMIT_HOSTLOCAL_Y_4(4HH, u8, widthlimit, gs.imageEndY); 
		/* sometimes wrong sizes are sent (tekken tag) */ 
		assert( gs.imageTransfer == -1 || TransmitPitch_4<u8>(nSize)/4 <= 2 ); 
	} 
	
	End: 
	if( i >= gs.imageEndY ) { 
		assert( gs.imageTransfer == -1 || i == gs.imageEndY ); 
		gs.imageTransfer = -1; 
		/*int start, end; 
		ZeroGS::GetRectMemAddress(start, end, gs.dstbuf.psm, gs.trxpos.dx, gs.trxpos.dy, gs.imageWnew, gs.imageHnew, gs.dstbuf.bp, gs.dstbuf.bw); 
		ZeroGS::g_MemTargs.ClearRange(start, end);*/ 
	} 
	else { 
		/* update new params */ 
		gs.imageY = i; 
		gs.imageX = j; 
	} 
	return (nSize * TransmitPitch_4<u8>(2) + nLeftOver)/2; 
} 
#else

DEFINE_TRANSFERLOCAL(32, u32, 2, 32, 8, 8, _, SwizzleBlock32);
DEFINE_TRANSFERLOCAL(32Z, u32, 2, 32, 8, 8, _, SwizzleBlock32);
DEFINE_TRANSFERLOCAL(24, u8, 8, 32, 8, 8, _24, SwizzleBlock24);
DEFINE_TRANSFERLOCAL(24Z, u8, 8, 32, 8, 8, _24, SwizzleBlock24);
DEFINE_TRANSFERLOCAL(16, u16, 4, 16, 16, 8, _, SwizzleBlock16);
DEFINE_TRANSFERLOCAL(16S, u16, 4, 16, 16, 8, _, SwizzleBlock16);
DEFINE_TRANSFERLOCAL(16Z, u16, 4, 16, 16, 8, _, SwizzleBlock16);
DEFINE_TRANSFERLOCAL(16SZ, u16, 4, 16, 16, 8, _, SwizzleBlock16);
DEFINE_TRANSFERLOCAL(8, u8, 4, 8, 16, 16, _, SwizzleBlock8);
DEFINE_TRANSFERLOCAL(4, u8, 8, 4, 32, 16, _4, SwizzleBlock4);
DEFINE_TRANSFERLOCAL(8H, u8, 4, 32, 8, 8, _, SwizzleBlock8H);
DEFINE_TRANSFERLOCAL(4HL, u8, 8, 32, 8, 8, _4, SwizzleBlock4HL);
DEFINE_TRANSFERLOCAL(4HH, u8, 8, 32, 8, 8, _4, SwizzleBlock4HH);

#endif

void TransferLocalHost32(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost24(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost16(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost16S(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost8(void* pbyMem, u32 nQWordSize)
{
}

void TransferLocalHost4(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost8H(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost4HL(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost4HH(void* pbyMem, u32 nQWordSize)
{
}

void TransferLocalHost32Z(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost24Z(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost16Z(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

void TransferLocalHost16SZ(void* pbyMem, u32 nQWordSize)
{FUNCLOG
}

#define FILL_BLOCK(bw, bh, ox, oy, mult, psm, psmcol) { \
	b.vTexDims = Vector(BLOCK_TEXWIDTH/(float)(bw), BLOCK_TEXHEIGHT/(float)bh, 0, 0); \
	b.vTexBlock = Vector((float)bw/BLOCK_TEXWIDTH, (float)bh/BLOCK_TEXHEIGHT, ((float)ox+0.2f)/BLOCK_TEXWIDTH, ((float)oy+0.05f)/BLOCK_TEXHEIGHT); \
	b.width = bw; \
	b.height = bh; \
	b.colwidth = bh / 4; \
	b.colheight = bw / 8; \
	b.bpp = 32/mult; \
	\
	b.pageTable = &g_pageTable##psm[0][0]; \
	b.blockTable = &g_blockTable##psm[0][0]; \
	b.columnTable = &g_columnTable##psmcol[0][0]; \
	assert( sizeof(g_pageTable##psm) == bw*bh*sizeof(g_pageTable##psm[0][0]) ); \
	psrcf = (float*)&vBlockData[0] + ox + oy * BLOCK_TEXWIDTH; \
	psrcw = (u16*)&vBlockData[0] + ox + oy * BLOCK_TEXWIDTH; \
	for(i = 0; i < bh; ++i) { \
		for(j = 0; j < bw; ++j) { \
			/* fill the table */ \
			u32 u = g_blockTable##psm[(i / b.colheight)][(j / b.colwidth)] * 64 * mult + g_columnTable##psmcol[i%b.colheight][j%b.colwidth]; \
			b.pageTable[i*bw+j] = u; \
			if( floatfmt ) { \
				psrcf[i*BLOCK_TEXWIDTH+j] = (float)(u) / (float)(GPU_TEXWIDTH*mult); \
			} \
			else { \
				psrcw[i*BLOCK_TEXWIDTH+j] = u; \
			} \
		} \
	} \
	\
	if( floatfmt ) { \
		assert( floatfmt ); \
		psrcv = (Vector*)&vBilinearData[0] + ox + oy * BLOCK_TEXWIDTH; \
		for(i = 0; i < bh; ++i) { \
			for(j = 0; j < bw; ++j) { \
				Vector* pv = &psrcv[i*BLOCK_TEXWIDTH+j]; \
				pv->x = psrcf[i*BLOCK_TEXWIDTH+j]; \
				pv->y = psrcf[i*BLOCK_TEXWIDTH+((j+1)%bw)]; \
				pv->z = psrcf[((i+1)%bh)*BLOCK_TEXWIDTH+j]; \
				pv->w = psrcf[((i+1)%bh)*BLOCK_TEXWIDTH+((j+1)%bw)]; \
			} \
		} \
	} \
	b.getPixelAddress = getPixelAddress##psm; \
	b.getPixelAddress_0 = getPixelAddress##psm##_0; \
	b.writePixel = writePixel##psm; \
	b.writePixel_0 = writePixel##psm##_0; \
	b.readPixel = readPixel##psm; \
	b.readPixel_0 = readPixel##psm##_0; \
	b.TransferHostLocal = TransferHostLocal##psm; \
	b.TransferLocalHost = TransferLocalHost##psm; \
} \

void BLOCK::FillBlocks(vector<char>& vBlockData, vector<char>& vBilinearData, int floatfmt)
{
	FUNCLOG
	vBlockData.resize(BLOCK_TEXWIDTH * BLOCK_TEXHEIGHT * (floatfmt ? 4 : 2));
	if( floatfmt )
		vBilinearData.resize(BLOCK_TEXWIDTH * BLOCK_TEXHEIGHT * sizeof(Vector));

	int i, j;
	BLOCK b;
	float* psrcf = NULL;
	u16* psrcw = NULL;
	Vector* psrcv = NULL;

	memset(m_Blocks, 0, sizeof(m_Blocks));

	// 32
	FILL_BLOCK(64, 32, 0, 0, 1, 32, 32);
	m_Blocks[PSMCT32] = b;

	// 24 (same as 32 except write/readPixel are different)
	m_Blocks[PSMCT24] = b;
	m_Blocks[PSMCT24].writePixel = writePixel24;
	m_Blocks[PSMCT24].writePixel_0 = writePixel24_0;
	m_Blocks[PSMCT24].readPixel = readPixel24;
	m_Blocks[PSMCT24].readPixel_0 = readPixel24_0;
	m_Blocks[PSMCT24].TransferHostLocal = TransferHostLocal24;
	m_Blocks[PSMCT24].TransferLocalHost = TransferLocalHost24;

	// 8H (same as 32 except write/readPixel are different)
	m_Blocks[PSMT8H] = b;
	m_Blocks[PSMT8H].writePixel = writePixel8H;
	m_Blocks[PSMT8H].writePixel_0 = writePixel8H_0;
	m_Blocks[PSMT8H].readPixel = readPixel8H;
	m_Blocks[PSMT8H].readPixel_0 = readPixel8H_0;
	m_Blocks[PSMT8H].TransferHostLocal = TransferHostLocal8H;
	m_Blocks[PSMT8H].TransferLocalHost = TransferLocalHost8H;

	m_Blocks[PSMT4HL] = b;
	m_Blocks[PSMT4HL].writePixel = writePixel4HL;
	m_Blocks[PSMT4HL].writePixel_0 = writePixel4HL_0;
	m_Blocks[PSMT4HL].readPixel = readPixel4HL;
	m_Blocks[PSMT4HL].readPixel_0 = readPixel4HL_0;
	m_Blocks[PSMT4HL].TransferHostLocal = TransferHostLocal4HL;
	m_Blocks[PSMT4HL].TransferLocalHost = TransferLocalHost4HL;

	m_Blocks[PSMT4HH] = b;
	m_Blocks[PSMT4HH].writePixel = writePixel4HH;
	m_Blocks[PSMT4HH].writePixel_0 = writePixel4HH_0;
	m_Blocks[PSMT4HH].readPixel = readPixel4HH;
	m_Blocks[PSMT4HH].readPixel_0 = readPixel4HH_0;
	m_Blocks[PSMT4HH].TransferHostLocal = TransferHostLocal4HH;
	m_Blocks[PSMT4HH].TransferLocalHost = TransferLocalHost4HH;

	// 32z
	FILL_BLOCK(64, 32, 64, 0, 1, 32Z, 32);
	m_Blocks[PSMT32Z] = b;

	// 24Z (same as 32Z except write/readPixel are different)
	m_Blocks[PSMT24Z] = b;
	m_Blocks[PSMT24Z].writePixel = writePixel24Z;
	m_Blocks[PSMT24Z].writePixel_0 = writePixel24Z_0;
	m_Blocks[PSMT24Z].readPixel = readPixel24Z;
	m_Blocks[PSMT24Z].readPixel_0 = readPixel24Z_0;
	m_Blocks[PSMT24Z].TransferHostLocal = TransferHostLocal24Z;
	m_Blocks[PSMT24Z].TransferLocalHost = TransferLocalHost24Z;

	// 16
	FILL_BLOCK(64, 64, 0, 32, 2, 16, 16);
	m_Blocks[PSMCT16] = b;

	// 16s
	FILL_BLOCK(64, 64, 64, 32, 2, 16S, 16);
	m_Blocks[PSMCT16S] = b;

	// 16z
	FILL_BLOCK(64, 64, 0, 96, 2, 16Z, 16);
	m_Blocks[PSMT16Z] = b;

	// 16sz
	FILL_BLOCK(64, 64, 64, 96, 2, 16SZ, 16);
	m_Blocks[PSMT16SZ] = b;

	// 8
	FILL_BLOCK(128, 64, 0, 160, 4, 8, 8);
	m_Blocks[PSMT8] = b;

	// 4
	FILL_BLOCK(128, 128, 0, 224, 8, 4, 4);
	m_Blocks[PSMT4] = b;
}