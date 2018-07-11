
#pragma once

#include "ColibriGui/ColibriRenderable.h"

COLIBRIGUI_ASSUME_NONNULL_BEGIN

namespace Colibri
{
	/** @ingroup Controls
	@class Editbox
	*/
	class Editbox : public Renderable
	{
		Label		*m_label;
		Label		*m_caret;
		/// It's in glyphs
		uint32_t	m_cursorPos;

	public:
		/// When true, multiline prevents Enter key from being
		/// used for activating the Widget's PrimaryAction
		///
		/// @remark	PUBLIC MEMEBER: CAN BE EDITED DIRECTLY
		///
		/// @see	ColibriManager::isTextMultiline
		bool		m_multiline;

		float		m_blinkTimer;

	protected:

		void showCaret();
		bool requiresActiveUpdate() const;

	public:
		Editbox( ColibriManager *manager );

		virtual void _initialize();
		virtual void _destroy();

		Label* getLabel();

		virtual void setState( States::States state, bool smartHighlight=true,
							   bool broadcastEnable=false );

		void _update( float timeSinceLast );

		virtual void _setTextEdit( const char *text, int32_t selectStart, int32_t selectLength );
		virtual void _setTextSpecialKey( uint32_t keyCode, uint16_t keyMod );
		virtual void _setTextInput( const char *text );
		virtual Ogre::Vector2 _getImeLocation();
		virtual bool isTextMultiline() const;
		virtual bool wantsTextInput() const;

		virtual void setTransformDirty();

		virtual void _notifyActionKeyMovement( Borders::Borders direction );
	};
}

COLIBRIGUI_ASSUME_NONNULL_END
