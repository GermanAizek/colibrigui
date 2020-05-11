
#include "ColibriGui/ColibriSlider.h"

#include "ColibriGui/ColibriManager.h"
#include "ColibriGui/ColibriSkinManager.h"

namespace Colibri
{

	Slider::Slider( ColibriManager *manager ) :
		Widget( manager ),
		IdObject( Ogre::Id::generateNewId<Progressbar>() ),
		m_sliderValue( 0.0f ),
		m_directionChangeAmount( 0.1f ),
		m_cursorOffset( 0.0f ),
		m_handleSize( 10.0f )
	{
		memset( m_layers, 0, sizeof( m_layers ) );

		m_clickable = true;
		m_keyboardNavigable = true;
		m_consumesScroll = true;

		m_autoSetNextWidget[Borders::Left] = false;
		m_autoSetNextWidget[Borders::Right] = false;
	}
	//-------------------------------------------------------------------------
	void Slider::_initialize()
	{
		for( size_t i = 0u; i < 2u; ++i )
		{
			m_layers[i] = m_manager->createWidget<Renderable>( this );
			m_layers[i]->_initialize();
		}

		getSliderLine()->_setSkinPack( m_manager->getDefaultSkin( SkinWidgetTypes::SliderLine ) );
		getSliderHandle()->_setSkinPack( m_manager->getDefaultSkin( SkinWidgetTypes::SliderHandle ) );

		Widget::_initialize();


		/*const Ogre::IdString skinPackName =
			m_manager->getDefaultSkinPackName( SkinWidgetTypes::SliderLine );
		const SkinManager *skinManager = m_manager->getSkinManager();

		const SkinPack *defaultSkinPack = skinManager->findSkinPack( skinPackName, LogSeverity::Fatal );

		if( !defaultSkinPack )
			return;*/
	}
	//-------------------------------------------------------------------------
	void Slider::_destroy()
	{
		Widget::_destroy();

		// m_layers[i] are children of us, so they will be destroyed by our super class
		for( size_t i = 0u; i < 2u; ++i )
			m_layers[i] = 0;
	}
	//-------------------------------------------------------------------------
	Renderable *colibrigui_nullable Slider::getSliderLine()
	{
		return m_layers[0];
	}
	//-------------------------------------------------------------------------
	Renderable *colibrigui_nullable Slider::getSliderHandle()
	{
		return m_layers[1];
	}
	//-------------------------------------------------------------------------
	void Slider::setVisualsEnabled( bool bEnabled )
	{
		for( size_t i = 0u; i < 2u; ++i )
			m_layers[i]->setVisualsEnabled( bEnabled );
	}
	//-------------------------------------------------------------------------
	bool Slider::isVisualsEnabled() const { return m_layers[0]->isVisualsEnabled(); }
	//-------------------------------------------------------------------------
	void Slider::setState( States::States state, bool smartHighlight, bool broadcastEnable )
	{
		Widget::setState( state, smartHighlight, broadcastEnable );

		if(state == States::Pressed)
		{
			_processCursorPosition( m_manager->getMouseCursorPosNdc(), true );
		}else m_cursorOffset = 0.0f;

		// Widget::setState did not re-enable children we control. Do it manually
		if( !broadcastEnable )
		{
			for( size_t i = 0u; i < 2u; ++i )
			{
				if( m_layers[i]->isDisabled() )
					m_layers[i]->setState( state, smartHighlight, false );
			}
		}
	}
	//-------------------------------------------------------------------------
	void Slider::updateSlider()
	{

		if( !m_layers[0] )
			return;  //_initialize hasn't been called yet

		const bool rightToLeft = m_manager->shouldSwapRTL( HorizWidgetDir::AutoLTR );

		const Ogre::Vector2 frameSize = getSize();
		static const float sliderLineHeight = 5.0f;

		m_handleSize = frameSize.y * 0.8f;

		// Slider line
		//const Ogre::Vector2 framePosition = getLocalTopLeft();
		const Ogre::Vector2 framePosition = Ogre::Vector2::ZERO;
		const float lineY = framePosition.y + (frameSize.y / 2.0f) - (sliderLineHeight / 2.0f);

		// Half a handle is added to the line on each side as padding.
		m_layers[0]->setTopLeft( Ogre::Vector2(framePosition.x + m_handleSize / 2.0f, lineY) );

		// Other than the padding, the width is used to its full, but the height is always a constant.
		const float reducedLineWidth = frameSize.x - m_handleSize;
		m_layers[0]->setSize( Ogre::Vector2(reducedLineWidth, sliderLineHeight) );


		// Slider handle
		m_layers[1]->setSize( Ogre::Vector2(m_handleSize, m_handleSize) );

		const float targetSliderValue = rightToLeft ? 1 - m_sliderValue : m_sliderValue;
		m_layers[1]->setTopLeft( Ogre::Vector2(framePosition.x + (reducedLineWidth * targetSliderValue), lineY - m_handleSize / 2.0f) );

		for( size_t i = 0u; i < 2u; ++i )
			m_layers[i]->updateDerivedTransformFromParent( false );
	}
	//-------------------------------------------------------------------------
	void Slider::setTransformDirty( uint32_t dirtyReason )
	{
		// Only update if our size is directly being changed, not our parent's
		if( ( dirtyReason & ( TransformDirtyParentCaller | TransformDirtyScale ) ) ==
			TransformDirtyScale )
		{
			updateSlider();
		}

		Widget::setTransformDirty( dirtyReason );
	}
	//-------------------------------------------------------------------------
	void Slider::_processCursorPosition( const Ogre::Vector2& pos, bool cursorBegin ){
		if( m_currentState == States::Pressed && this->intersects( pos ) )
		{
			const bool rightToLeft = m_manager->shouldSwapRTL( HorizWidgetDir::AutoLTR );

			const float sliderWidth = getSliderLine()->getDerivedBottomRight().x - getSliderLine()->getDerivedTopLeft().x;
			const float mouseRelativeX = pos.x - m_derivedTopLeft.x;
			float posX = mouseRelativeX / sliderWidth;
			if(rightToLeft)
				posX = 1 - posX;

			if( cursorBegin && getSliderHandle()->intersects(pos) )
			{
				// The user actually clicked on the handle, rather than part of the line.
				// If this happens, apply an offset to the mouse movements, so the handle doesn't jump.
				m_cursorOffset = posX - m_sliderValue;
			}

			setValue(posX - m_cursorOffset);
		}
	}
	//-------------------------------------------------------------------------
	void Slider::notifyCursorMoved( const Ogre::Vector2& posNDC ){
		_processCursorPosition( posNDC );
	}
	//-------------------------------------------------------------------------
	void Slider::_notifyActionKeyMovement( Borders::Borders direction )
	{
		const bool rightToLeft = m_manager->shouldSwapRTL( HorizWidgetDir::AutoLTR );
		const float targetDirectionAmount = rightToLeft ? -m_directionChangeAmount : m_directionChangeAmount;

		if( direction == Borders::Left )
			setValue( m_sliderValue - targetDirectionAmount );
		else if( direction == Borders::Right )
			setValue( m_sliderValue + targetDirectionAmount );
	}
	//-------------------------------------------------------------------------
	void Slider::setValue( float value )
	{
		if( value < 0.0f ) value = 0.0f;
		else if( value > 1.0f ) value = 1.0f;

		m_sliderValue = value;

		updateSlider();

		callActionListeners( Action::ValueChanged );
	}
}  // namespace Colibri
