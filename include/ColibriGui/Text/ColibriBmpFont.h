
#pragma once

#include "ColibriGui/ColibriGuiPrerequisites.h"

#include "OgreImage2.h"
#include "OgreVector2.h"

COLIBRI_ASSUME_NONNULL_BEGIN

namespace Ogre
{
	class HlmsColibri;
}

namespace Colibri
{
	struct BmpChar
	{
		uint32_t id;
		uint16_t x;
		uint16_t y;
		uint16_t width;
		uint16_t height;
		uint16_t xoffset;
		uint16_t yoffset;
		uint16_t xadvance;

		bool operator<( const BmpChar &other ) const { return this->id < other.id; }
	};

	struct BmpGlyph
	{
		bool           isNewline;
		bool           isTab;
		uint16_t       xoffset;
		uint16_t       yoffset;
		uint16_t       width;
		uint16_t       height;
		BmpChar const *bmpChar;
	};

	typedef std::vector<BmpGlyph> BmpGlyphVec;

	class BmpFont
	{
	protected:
		std::string                              m_textureName;
		Ogre::TextureGpu *colibri_nullable    m_fontTexture;
		Ogre::HlmsDatablock *colibri_nullable m_datablock;

		FontSize m_fontSize;

		std::vector<BmpChar> m_chars;

		BmpChar m_emptyChar;

		uint16_t m_fontIdx;

		bool m_bilinearFilter;

		/** Populates m_chars
		@param inFntDat
			Font file string. It's actually const, but we temporarily swap
			its contents as a compiler optimization
		*/
		void parseFntFile( std::vector<char> &inFntData );

	public:
		BmpFont( const char *fontLocation, ShaperManager *shaperManager );
		~BmpFont();

		/**
		@brief setBilinearFilter
			Whether to use bilinear or point filtering
		@param bBilinearFilter
			True to use bilinear (default)
		*/
		void setBilinearFilter( bool bBilinearFilter );

		void setOgre( Ogre::HlmsColibri *hlms, Ogre::TextureGpuManager *textureManager );

		void renderString( const std::string &utf8Str, BmpGlyphVec &outShapes ) const;
		void renderCodepoint( const uint32_t codepoint, BmpGlyphVec &outShapes ) const;

		BmpGlyph renderCodepoint( const uint32_t codepoint ) const;

		Ogre::Vector4 getInvResolution() const;

		FontSize getBakedFontSize() const { return m_fontSize; }

		/// This pointer can be casted to HlmsColibriDatablock
		Ogre::HlmsDatablock *colibri_nullable getDatablock() const { return m_datablock; }
	};
}  // namespace Colibri

COLIBRI_ASSUME_NONNULL_END
