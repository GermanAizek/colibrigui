
#include "CrystalGui/Text/CrystalShaperManager.h"
#include "CrystalGui/Text/CrystalShaper.h"
#include "CrystalGui/CrystalManager.h"

#include "CrystalGui/Ogre/OgreHlmsCrystal.h"

#include "Vao/OgreTexBufferPacked.h"

#include "OgreLwString.h"

#include "ft2build.h"
#include "freetype/freetype.h"

#include "unicode/ubidi.h"
#include "unicode/unistr.h"

namespace Crystal
{
	ShaperManager::ShaperManager( CrystalManager *crystalManager ) :
		m_ftLibrary( 0 ),
		m_crystalManager( crystalManager ),
		m_glyphAtlas( 0 ),
		m_offsetPtr( 0 ),
		m_atlasCapacity( 32 ),
		m_bidi( 0 ),
		m_defaultDirection( UBIDI_DEFAULT_LTR ),
		m_useVerticalLayoutWhenAvailable( false ),
		m_glyphAtlasBuffer( 0 ),
		m_hlms( 0 ),
		m_vaoManager( 0 )
	{
		FT_Error errorCode = FT_Init_FreeType( &m_ftLibrary );
		if( errorCode )
		{
			LogListener *log = this->getLogListener();
			char tmpBuffer[512];
			Ogre::LwString errorMsg( Ogre::LwString::FromEmptyPointer( tmpBuffer, sizeof(tmpBuffer) ) );

			errorMsg.clear();
			errorMsg.a( "[Freetype2 error] Could not initialize Freetype errorCode: ",
						errorCode, " Desc: ", ShaperManager::getErrorMessage( errorCode ) );
			log->log( errorMsg.c_str(), LogSeverity::Fatal );
		}

		m_bidi = ubidi_open();
	}
	//-------------------------------------------------------------------------
	ShaperManager::~ShaperManager()
	{
		if( !m_shapers.empty() )
		{
			ShaperVec::const_iterator itor = m_shapers.begin() + 1u;
			ShaperVec::const_iterator end  = m_shapers.end();

			while( itor != end )
				delete *itor++;

			m_shapers.clear();
		}

		if( m_glyphAtlas )
		{
			free( m_glyphAtlas );
			m_glyphAtlas = 0;
		}

		setOgre( 0, 0 );

		ubidi_close( m_bidi );
		m_bidi = 0;

		FT_Done_FreeType( m_ftLibrary );
		m_ftLibrary = 0;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::setOgre( Ogre::HlmsCrystal * crystalgui_nullable hlms,
								 Ogre::VaoManager * crystalgui_nullable vaoManager )
	{
		if( m_hlms )
			m_hlms->setGlyphAtlasBuffer( 0 );
		if( m_vaoManager && m_glyphAtlasBuffer )
		{
			m_vaoManager->destroyTexBuffer( m_glyphAtlasBuffer );
			m_glyphAtlasBuffer = 0;
		}

		m_hlms = hlms;
		m_vaoManager = vaoManager;
	}
	//-------------------------------------------------------------------------
	Shaper* ShaperManager::addShaper( uint32_t /*hb_script_t*/ script, const char *fontPath,
									  const std::string &language )
	{
		Shaper *shaper = new Shaper( static_cast<hb_script_t>( script ), fontPath, language, this );
		if( m_shapers.empty() )
			m_shapers.push_back( shaper );
		m_shapers.push_back( shaper );

		return shaper;
	}
	//-------------------------------------------------------------------------
	LogListener* ShaperManager::getLogListener() const
	{
		return m_crystalManager->getLogListener();
	}
	//-------------------------------------------------------------------------
	void ShaperManager::growAtlas( size_t sizeBytes )
	{
		m_atlasCapacity = std::max( m_offsetPtr + sizeBytes,
									m_atlasCapacity + (m_atlasCapacity >> 1u) + 1u );
		m_glyphAtlas = reinterpret_cast<uint8_t*>( realloc( m_glyphAtlas, m_atlasCapacity ) );
	}
	//-------------------------------------------------------------------------
	size_t ShaperManager::getAtlasOffset( size_t sizeBytes )
	{
		//Get smallest available free range
		RangeVec::iterator end = m_freeRanges.end();
		RangeVec::iterator bestRange = end;

		{
			RangeVec::iterator itor = m_freeRanges.begin();
			while( itor != end )
			{
				if( sizeBytes < itor->size && (bestRange == end || bestRange->size > itor->size) )
					bestRange = itor;
				++itor;
			}
		}

		size_t retVal = 0;

		if( bestRange != end )
		{
			retVal = bestRange->offset;
			bestRange->offset	+= sizeBytes;
			bestRange->size		-= sizeBytes;
			if( bestRange->size == 0 )
				Ogre::efficientVectorRemove( m_freeRanges, bestRange );
		}
		else
		{
			//Couldn't find free space in fragmented pool.
			if( m_offsetPtr + sizeBytes > m_atlasCapacity )
			{
				//We're out of space. First check if we can steal another slot.
				CachedGlyphMap::iterator end  = m_glyphCache.end();
				CachedGlyphMap::iterator bestUnusedGlyph = end;

				for( size_t i=0; i<2u && bestUnusedGlyph == end; ++i )
				{
					CachedGlyphMap::iterator itor = m_glyphCache.begin();
					while( itor != end )
					{
						if( !itor->second.refCount && itor->second.getSizeBytes() >= sizeBytes &&
							(bestUnusedGlyph == end ||
							 itor->second.getSizeBytes() < bestUnusedGlyph->second.getSizeBytes()) )
						{
							bestUnusedGlyph = itor;
						}
						++itor;
					}

					//Not found? Try again, this time with all unused glyphs removed and merged.
					//We may have two contiguous unused glyphs that are big enough to hold
					//this new glyph, but weren't big enough individually.
					if( i == 0 && bestUnusedGlyph == end )
						flushReleasedGlyphs();
				}

				if( bestUnusedGlyph == end )
				{
					//Cannot steal. Grow the atlas, advance the pointer and get a fresh region
					growAtlas( sizeBytes );
					retVal = m_offsetPtr;
					m_offsetPtr += sizeBytes;
				}
				else
				{
					//Steal successful! Put the unused glyph back into the pool and try again
					destroyGlyph( bestUnusedGlyph );
					retVal = getAtlasOffset( sizeBytes );
				}
			}
			else
			{
				//Advance the pointer and get a fresh region
				retVal = m_offsetPtr;
				m_offsetPtr += sizeBytes;
			}
		}

		return retVal;
	}
	//-------------------------------------------------------------------------
	CachedGlyph* ShaperManager::createGlyph( FT_Face font, uint32_t codepoint, uint32_t ptSize )
	{
		FT_Error errorCode = FT_Load_Glyph( font, codepoint, FT_LOAD_DEFAULT );
		if( crystalgui_unlikely( errorCode ) )
		{
			LogListener *log = getLogListener();
			char tmpBuffer[512];
			Ogre::LwString errorMsg( Ogre::LwString::FromEmptyPointer( tmpBuffer, sizeof(tmpBuffer) ) );

			errorMsg.clear();
			errorMsg.a( "[Freetype2 error] Could not load glyph for codepoint ", codepoint,
						" errorCode: ", errorCode, " Desc: ",
						ShaperManager::getErrorMessage( errorCode ) );
			log->log( errorMsg.c_str(), LogSeverity::Warning );
		}

		//Rasterize the glyph
		FT_GlyphSlot slot = font->glyph;
		FT_Render_Glyph( slot, FT_RENDER_MODE_NORMAL );

		FT_Bitmap ftBitmap = slot->bitmap;

		//Create a cache entry
		CachedGlyph newGlyph;
		newGlyph.codepoint	= codepoint;
		newGlyph.ptSize		= ptSize;
		newGlyph.bearingX	= slot->bitmap_left;
		newGlyph.bearingY	= slot->bitmap_top;
		newGlyph.width		= static_cast<uint16_t>( ftBitmap.width );
		newGlyph.height		= static_cast<uint16_t>( ftBitmap.rows );
		newGlyph.offsetStart= getAtlasOffset( newGlyph.getSizeBytes() );
		newGlyph.newlineSize= slot->metrics.height / 64.0f;
		newGlyph.refCount	= 0;

		const uint64_t glyphKey = ((uint64_t)codepoint << 32ul) | ((uint64_t)ptSize);
		std::pair<CachedGlyphMap::iterator, bool> pair =
				m_glyphCache.insert( std::pair<uint64_t, CachedGlyph>( glyphKey, newGlyph ) );

		//Copy the rasterized results to our atlas
		memcpy( m_glyphAtlas + newGlyph.offsetStart, ftBitmap.buffer, newGlyph.getSizeBytes() );
		{
			//Schedule a transfer to the GPU.
			Range dirtyRange;
			dirtyRange.offset	= newGlyph.offsetStart;
			dirtyRange.size		= newGlyph.getSizeBytes();
			m_dirtyRanges.push_back( dirtyRange );
		}

		return &pair.first->second;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::destroyGlyph( CachedGlyphMap::iterator glyphIt )
	{
		CachedGlyph &glyph = glyphIt->second;

		if( glyph.offsetStart + glyph.getSizeBytes() == m_offsetPtr )
		{
			//Easy case. LIFO.
			m_offsetPtr -= glyph.getSizeBytes();
		}
		else
		{
			Range freeRange;
			freeRange.offset= glyph.offsetStart;
			freeRange.size	= glyph.getSizeBytes();
			m_freeRanges.push_back( freeRange );
			mergeContiguousBlocks( m_freeRanges.end() - 1u, m_freeRanges );
		}

		m_glyphCache.erase( glyphIt );
	}
	//-------------------------------------------------------------------------
	void ShaperManager::mergeContiguousBlocks( RangeVec::iterator blockToMerge,
											   RangeVec &blocks )
	{
		RangeVec::iterator itor = blocks.begin();
		RangeVec::iterator end  = blocks.end();

		while( itor != end )
		{
			if( itor->offset + itor->size == blockToMerge->offset )
			{
				itor->size += blockToMerge->size;
				size_t idx = itor - blocks.begin();

				//When blockToMerge is the last one, its index won't be the same
				//after removing the other iterator, they will swap.
				if( idx == blocks.size() - 1 )
					idx = blockToMerge - blocks.begin();

				Ogre::efficientVectorRemove( blocks, blockToMerge );

				blockToMerge = blocks.begin() + idx;
				itor = blocks.begin();
				end  = blocks.end();
			}
			else if( blockToMerge->offset + blockToMerge->size == itor->offset )
			{
				blockToMerge->size += itor->size;
				size_t idx = blockToMerge - blocks.begin();

				//When blockToMerge is the last one, its index won't be the same
				//after removing the other iterator, they will swap.
				if( idx == blocks.size() - 1 )
					idx = itor - blocks.begin();

				Ogre::efficientVectorRemove( blocks, itor );

				blockToMerge = blocks.begin() + idx;
				itor = blocks.begin();
				end  = blocks.end();
			}
			else
			{
				++itor;
			}
		}
	}
	//-------------------------------------------------------------------------
	const CachedGlyph* ShaperManager::acquireGlyph( FT_Face font, uint32_t codepoint, uint32_t ptSize )
	{
		CachedGlyph *retVal = 0;

		const uint64_t glyphKey = ((uint64_t)codepoint << 32ul) | ((uint64_t)ptSize);
		CachedGlyphMap::iterator itor = m_glyphCache.find( glyphKey );

		if( itor != m_glyphCache.end() )
			retVal = &itor->second;
		else
			retVal = createGlyph( font, codepoint, ptSize );

		++retVal->refCount;

		return retVal;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::addRefCount( const CachedGlyph *cachedGlyph )
	{
		const uint64_t glyphKey = ((uint64_t)cachedGlyph->codepoint << 32ul) |
								  ((uint64_t)cachedGlyph->ptSize);

		CRYSTAL_ASSERT_MEDIUM( m_glyphCache.find( glyphKey ) != m_glyphCache.end() &&
							   "Invalid glyph cache entry. Use-after-free perhaps?" );

		CachedGlyph *nonConstCachedGlyph = const_cast<CachedGlyph*>( cachedGlyph );
		++nonConstCachedGlyph->refCount;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::releaseGlyph( uint32_t codepoint, uint32_t ptSize )
	{
		const uint64_t glyphKey = ((uint64_t)codepoint << 32ul) | ((uint64_t)ptSize);

		CachedGlyphMap::iterator itor = m_glyphCache.find( glyphKey );

		CRYSTAL_ASSERT_LOW( itor != m_glyphCache.end() &&
							"Invalid glyph cache entry not found. Use-after-free perhaps?" );
		CRYSTAL_ASSERT_LOW( itor->second.refCount > 0 );

		if( itor != m_glyphCache.end() && itor->second.refCount > 0 )
			--itor->second.refCount;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::releaseGlyph( const CachedGlyph *cachedGlyph )
	{
		const uint64_t glyphKey = ((uint64_t)cachedGlyph->codepoint << 32ul) |
								  ((uint64_t)cachedGlyph->ptSize);

		CRYSTAL_ASSERT_MEDIUM( m_glyphCache.find( glyphKey ) != m_glyphCache.end() &&
							   "Invalid glyph cache entry. Use-after-free perhaps?" );
		CRYSTAL_ASSERT_LOW( cachedGlyph->refCount > 0 );

		CachedGlyph *nonConstCachedGlyph = const_cast<CachedGlyph*>( cachedGlyph );
		if( nonConstCachedGlyph->refCount > 0 )
			--nonConstCachedGlyph->refCount;
	}
	//-------------------------------------------------------------------------
	void ShaperManager::flushReleasedGlyphs()
	{
		CachedGlyphMap::iterator itor = m_glyphCache.begin();
		CachedGlyphMap::iterator end  = m_glyphCache.end();

		while( itor != end )
		{
			if( !itor->second.refCount )
			{
				CachedGlyphMap::iterator toDelete = itor++;
				destroyGlyph( toDelete );
			}
			else
			{
				++itor;
			}
		}
	}
	//-------------------------------------------------------------------------
	void ShaperManager::renderString( const char *utf8Str, const RichText &richText,
									  VertReadingDir::VertReadingDir vertReadingDir,
									  ShapedGlyphVec &outShapes )
	{
		UnicodeString uStr( utf8Str, (int32_t)richText.length );

		UErrorCode errorCode = U_ZERO_ERROR;
		ubidi_setPara( m_bidi, uStr.getBuffer(), uStr.length(), m_defaultDirection, 0, &errorCode );

		if( crystalgui_unlikely( !U_SUCCESS(errorCode) ) )
		{
			LogListener *log = this->getLogListener();
			char tmpBuffer[512];
			Ogre::LwString errorMsg( Ogre::LwString::FromEmptyPointer( tmpBuffer, sizeof(tmpBuffer) ) );

			errorMsg.clear();
			errorMsg.a( "[UBiDi error] Error analyzing text. Error code: ", errorCode,
						" Desc: ", u_errorName( errorCode ), "\n[UBiDi error] String:" );
			log->log( errorMsg.c_str(), LogSeverity::Warning );
			log->log( utf8Str, LogSeverity::Warning );
			return;
		}

		Shaper *shaper = 0;
		if( crystalgui_unlikely( richText.font >= m_shapers.size() ) )
		{
			LogListener *log = this->getLogListener();
			char tmpBuffer[512];
			Ogre::LwString errorMsg( Ogre::LwString::FromEmptyPointer( tmpBuffer, sizeof(tmpBuffer) ) );

			errorMsg.clear();
			errorMsg.a( "[ShaperManager::renderString] RichText wants font ", richText.font,
						" but there's only ", (uint32_t)m_shapers.size(), " fonts installed" );
			log->log( errorMsg.c_str(), LogSeverity::Error );

			shaper = m_shapers[0];
		}
		else
			shaper = m_shapers[richText.font];

		UnicodeString uniStr( false, ubidi_getText( m_bidi ), ubidi_getLength( m_bidi ) );

		const int32_t numBlocks = ubidi_countRuns( m_bidi, &errorCode );
		for( size_t i=0; i<numBlocks; ++i )
		{
			int32_t logicalStart, length;
			UBiDiDirection dir = ubidi_getVisualRun( m_bidi, i, &logicalStart, &length );

			UnicodeString temp = uniStr.tempSubString( logicalStart, length );

			hb_direction_t hbDir = dir == UBIDI_LTR ? HB_DIRECTION_LTR : HB_DIRECTION_RTL;

			if( (vertReadingDir == VertReadingDir::IfNeededTTB && m_useVerticalLayoutWhenAvailable) ||
				vertReadingDir == VertReadingDir::ForceTTB )
			{
				hbDir = HB_DIRECTION_TTB;
			}

			const uint16_t *utf16Str = temp.getBuffer();
			shaper->setFontSize26d6( richText.ptSize );
			shaper->renderString( utf16Str, temp.length(), hbDir, outShapes );
		}
	}
	//-------------------------------------------------------------------------
	void ShaperManager::updateGpuBuffers()
	{
		if( !m_glyphAtlasBuffer ||
			m_atlasCapacity !=
			m_glyphAtlasBuffer->getNumElements() * m_glyphAtlasBuffer->getBytesPerElement() )
		{
			//Local buffer has changed (i.e. growAtlas was called). Realloc the GPU buffer.
			if( m_glyphAtlasBuffer )
				m_vaoManager->destroyTexBuffer( m_glyphAtlasBuffer );
			m_glyphAtlasBuffer = m_vaoManager->createTexBuffer( Ogre::PF_L8, m_atlasCapacity,
																Ogre::BT_DEFAULT, 0, false );
			m_hlms->setGlyphAtlasBuffer( m_glyphAtlasBuffer );

			m_glyphAtlasBuffer->upload( m_glyphAtlas, 0, m_offsetPtr );
			m_dirtyRanges.clear();
		}
		else
		{
			RangeVec::const_iterator itor = m_dirtyRanges.begin();
			RangeVec::const_iterator end  = m_dirtyRanges.end();

			while( itor != end )
			{
				m_glyphAtlasBuffer->upload( m_glyphAtlas + itor->offset, itor->offset, itor->size );
				++itor;
			}

			m_dirtyRanges.clear();
		}
	}
	//-------------------------------------------------------------------------
	const char* ShaperManager::getErrorMessage( FT_Error errorCode )
	{
		#undef __FTERRORS_H__
		#define FT_ERRORDEF( e, v, s )  case e: return s;
		#define FT_ERROR_START_LIST     switch( errorCode ) {
		#define FT_ERROR_END_LIST       }
		#include FT_ERRORS_H
		return "(Unknown error)";
	}
	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	size_t CachedGlyph::getSizeBytes() const
	{
		return this->width * this->height;
	}
	/*bool CachedGlyph::operator < ( const CachedGlyph &other ) const
	{
		const uint64_t thisKey = ((uint64_t)this->codepoint << 32ul) | ((uint64_t)this->ptSize);
		const uint64_t otherKey = ((uint64_t)other.codepoint << 32ul) | ((uint64_t)other.ptSize);
		return thisKey < otherKey;
	}
	bool operator < ( const CachedGlyph &a, const uint64_t &codePointSize )
	{
		const uint64_t aKey = ((uint64_t)a.codepoint << 32ul) | ((uint64_t)a.ptSize);
		return aKey < codePointSize;
	}
	bool operator < ( const uint64_t &codePointSize, const CachedGlyph &b )
	{
		const uint64_t bKey = ((uint64_t)b.codepoint << 32ul) | ((uint64_t)b.ptSize);
		return codePointSize < bKey;
	}*/
}
