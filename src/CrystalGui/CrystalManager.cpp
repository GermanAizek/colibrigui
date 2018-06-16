
#include "CrystalGui/CrystalManager.h"

#include "CrystalGui/CrystalWindow.h"
#include "CrystalGui/CrystalSkinManager.h"
#include "CrystalGui/CrystalLabel.h"

#include "CrystalGui/Text/CrystalShaperManager.h"

#include "CrystalGui/Ogre/CrystalOgreRenderable.h"
#include "CrystalGui/Ogre/OgreHlmsCrystal.h"
#include "CrystalGui/Ogre/OgreHlmsCrystalDatablock.h"
#include "Vao/OgreVaoManager.h"
#include "Vao/OgreVertexArrayObject.h"
#include "Vao/OgreIndirectBufferPacked.h"
#include "Math/Array/OgreObjectMemoryManager.h"
#include "OgreHlmsManager.h"
#include "OgreHlms.h"
#include "OgreRoot.h"
#include "CommandBuffer/OgreCommandBuffer.h"
#include "CommandBuffer/OgreCbDrawCall.h"

namespace Crystal
{
	static LogListener DefaultLogListener;
	static const Ogre::HlmsCache c_dummyCache( 0, Ogre::HLMS_MAX, Ogre::HlmsPso() );

	const std::string CrystalManager::c_defaultTextDatablockNames[States::NumStates] =
	{
		"# Crystal Disabled Text #",
		"# Crystal Idle Text #",
		"# Crystal HighlightedCursor Text #",
		"# Crystal HighlightedButton Text #",
		"# Crystal HighlightedButtonAndCursor Text #",
		"# Crystal Pressed Text #"
	};

	CrystalManager::CrystalManager( LogListener *logListener ) :
		m_numWidgets( 0 ),
		m_numLabels( 0 ),
		m_numTextGlyphs( 0u ),
		m_logListener( &DefaultLogListener ),
		m_swapRTLControls( false ),
		m_windowNavigationDirty( false ),
		m_root( 0 ),
		m_vaoManager( 0 ),
		m_objectMemoryManager( 0 ),
		m_sceneManager( 0 ),
		m_vao( 0 ),
		m_indirectBuffer( 0 ),
		m_commandBuffer( 0 ),
		m_allowingScrollAlways( false ),
		m_allowingScrollGestureWhileButtonDown( false ),
		m_mouseCursorButtonDown( false ),
		m_mouseCursorPosNdc( Ogre::Vector2( -2.0f, -2.0f ) ),
		m_primaryButtonDown( false ),
		m_keyDirDown( Borders::NumBorders ),
		m_keyRepeatWaitTimer( 0.0f ),
		m_keyRepeatDelay( 0.5f ),
		m_timeDelayPerKeyStroke( 0.1f ),
		m_skinManager( 0 ),
		m_shaperManager( 0 )
	{
		memset( m_defaultTextDatablock, 0, sizeof(m_defaultTextDatablock) );
		memset( m_defaultSkins, 0, sizeof(m_defaultSkins) );

		setLogListener( logListener );

		setCanvasSize( Ogre::Vector2( 1.0f ), Ogre::Vector2( 1.0f / 1600.0f, 1.0f / 900.0f ),
					   Ogre::Vector2( 1600.0f, 900.0f ) );

		m_skinManager = new SkinManager( this );

		m_shaperManager = new ShaperManager( this );
	}
	//-------------------------------------------------------------------------
	CrystalManager::~CrystalManager()
	{
		delete m_shaperManager;
		m_shaperManager = 0;

		setOgre( 0, 0, 0 );
		delete m_skinManager;
		m_skinManager = 0;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setLogListener( LogListener *logListener )
	{
		m_logListener = logListener;
		if( !m_logListener )
			m_logListener = &DefaultLogListener;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::loadSkins( const char *fullPath )
	{
		m_skinManager->loadSkins( fullPath );
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setOgre( Ogre::Root * crystalgui_nullable root,
								  Ogre::VaoManager * crystalgui_nullable vaoManager,
								  Ogre::SceneManager * crystalgui_nullable sceneManager )
	{
		delete m_commandBuffer;
		m_commandBuffer = 0;
		if( m_indirectBuffer )
		{
			if( m_indirectBuffer->getMappingState() != Ogre::MS_UNMAPPED )
				m_indirectBuffer->unmap( Ogre::UO_UNMAP_ALL );
			m_vaoManager->destroyIndirectBuffer( m_indirectBuffer );
			m_indirectBuffer = 0;
		}
		if( m_vao )
		{
			Ogre::CrystalOgreRenderable::destroyVao( m_vao, m_vaoManager );
			m_vao = 0;
		}
		/*if( m_defaultIndexBuffer )
		{
			m_vaoManager->destroyIndexBuffer( m_defaultIndexBuffer );
			m_defaultIndexBuffer = 0;
		}*/
		delete m_objectMemoryManager;
		m_objectMemoryManager = 0;

		m_root = root;
		m_vaoManager = vaoManager;
		m_sceneManager = sceneManager;

		if( vaoManager )
		{
			m_objectMemoryManager = new Ogre::ObjectMemoryManager();
			//m_defaultIndexBuffer = Ogre::CrystalOgreRenderable::createIndexBuffer( vaoManager );
			m_vao = Ogre::CrystalOgreRenderable::createVao( 6u * 9u, vaoManager );
			m_textVao = Ogre::CrystalOgreRenderable::createTextVao( 6u * 16u, vaoManager );
			size_t requiredBytes = 1u * sizeof( Ogre::CbDrawStrip );
			m_indirectBuffer = m_vaoManager->createIndirectBuffer( requiredBytes,
																   Ogre::BT_DYNAMIC_PERSISTENT,
																   0, false );
			m_commandBuffer = new Ogre::CommandBuffer();
			m_commandBuffer->setCurrentRenderSystem( m_sceneManager->getDestinationRenderSystem() );
		}

		if( m_shaperManager )
		{
			Ogre::HlmsCrystal *hlmsCrystal = 0;
			if( m_root )
			{
				Ogre::HlmsManager *hlmsManager = m_root->getHlmsManager();
				Ogre::Hlms *hlms = hlmsManager->getHlms( Ogre::HLMS_UNLIT );
				CRYSTAL_ASSERT_HIGH( dynamic_cast<Ogre::HlmsCrystal*>( hlms ) );
				hlmsCrystal = static_cast<Ogre::HlmsCrystal*>( hlms );

				Ogre::HlmsMacroblock macroblock;
				Ogre::HlmsBlendblock blendblock;

				macroblock.mDepthCheck = false;
				macroblock.mDepthWrite = false;
				blendblock.setBlendType( Ogre::SBT_TRANSPARENT_ALPHA );

				for( size_t i=0; i<States::NumStates; ++i )
				{
					m_defaultTextDatablock[i] = hlms->createDatablock( c_defaultTextDatablockNames[i],
																	   c_defaultTextDatablockNames[i],
																	   macroblock, blendblock,
																	   Ogre::HlmsParamVec() );
				}

				CRYSTAL_ASSERT_HIGH( dynamic_cast<Ogre::HlmsCrystalDatablock*>(
										 m_defaultTextDatablock[States::Disabled] ) );
				Ogre::HlmsCrystalDatablock *datablock = static_cast<Ogre::HlmsCrystalDatablock*>(
															m_defaultTextDatablock[States::Disabled] );
				datablock->setUseColour( true );
				datablock->setColour( Ogre::ColourValue( 0.8f, 0.8f, 0.8f, 0.2f ) );
			}
			m_shaperManager->setOgre( hlmsCrystal, vaoManager );
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setSwapRTLControls( bool swapRtl )
	{
		m_swapRTLControls = swapRtl;

		WindowVec::const_iterator itor = m_windows.begin();
		WindowVec::const_iterator end  = m_windows.end();

		while( itor != end )
		{
			(*itor)->setTransformDirty();
			++itor;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setDefaultSkins(
			std::string defaultSkinPacks[SkinWidgetTypes::NumSkinWidgetTypes] )
	{
		const SkinInfoMap &skins = m_skinManager->getSkins();
		const SkinPackMap &skinPacks = m_skinManager->getSkinPacks();

		for( size_t widgetType=0; widgetType<SkinWidgetTypes::NumSkinWidgetTypes; ++widgetType )
		{
			const std::string &skinName = defaultSkinPacks[widgetType];

			if( !skinName.empty() )
			{
				SkinPackMap::const_iterator itor = skinPacks.find( skinName );
				if( itor != skinPacks.end() )
				{
					const SkinPack &pack = itor->second;

					for( size_t i=0; i<States::NumStates; ++i )
					{
						SkinInfoMap::const_iterator itSkinInfo = skins.find( pack.skinInfo[i] );
						if( itSkinInfo != skins.end() )
							m_defaultSkins[widgetType][i] = &itSkinInfo->second;
					}
				}
			}
		}
	}
	//-------------------------------------------------------------------------
	SkinInfo const * crystalgui_nonnull const * crystalgui_nullable CrystalManager::getDefaultSkin(
			SkinWidgetTypes::SkinWidgetTypes widgetType ) const
	{
		return m_defaultSkins[widgetType];
	}
	//-------------------------------------------------------------------------
	Ogre::HlmsDatablock * crystalgui_nullable
	CrystalManager::getDefaultTextDatablock( States::States state ) const
	{
		return m_defaultTextDatablock[state];
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setCanvasSize( const Ogre::Vector2 &canvasSize,
										const Ogre::Vector2 &pixelSize,
										const Ogre::Vector2 &windowResolution )
	{
		m_canvasSize = canvasSize;
		m_invCanvasSize2x = 2.0f / canvasSize;
		m_pixelSize = pixelSize / canvasSize;
		m_pixelSize2x = (2.0f * pixelSize) / canvasSize;
		m_halfWindowResolution	= windowResolution / 2.0f;
		m_invWindowResolution2x = 2.0f / windowResolution;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::updateWidgetsFocusedByCursor()
	{
		const Ogre::Vector2 newPosNdc = m_mouseCursorPosNdc;

		FocusPair focusedPair;

		//The first window that our button is touching wins. We go in LIFO order.
		WindowVec::const_reverse_iterator ritor = m_windows.rbegin();
		WindowVec::const_reverse_iterator rend  = m_windows.rend();

		while( ritor != rend && !focusedPair.widget )
		{
			focusedPair = (*ritor)->_setIdleCursorMoved( newPosNdc );
			++ritor;
		}

		if( m_cursorFocusedPair.widget != focusedPair.widget )
		{
			//Do not steal focus from keyboard (by only moving the cursor) if we're holding
			//the main key button down (clicking does steal the focus from keyboard in
			//setMouseCursorPressed)
			const bool oldFullyFocusedByKey = m_primaryButtonDown &&
											  m_keyboardFocusedPair.widget == m_cursorFocusedPair.widget;
			const bool newFullyFocusedByKey = m_primaryButtonDown &&
											  m_keyboardFocusedPair.widget == focusedPair.widget;

			if( m_cursorFocusedPair.widget && !oldFullyFocusedByKey )
			{
				if( m_cursorFocusedPair.widget != m_keyboardFocusedPair.widget )
				{
					m_cursorFocusedPair.widget->setState( States::Idle );
				}
				else
					m_cursorFocusedPair.widget->setState( States::HighlightedButton, false );
				m_cursorFocusedPair.widget->callActionListeners( Action::Cancel );
			}

			if( focusedPair.widget && !newFullyFocusedByKey )
			{
				if( !m_mouseCursorButtonDown || !focusedPair.widget->isPressable() )
				{
					if( m_mouseCursorButtonDown )
					{
						//This call may end up calling focusedPair.widget->getParent()->setState()
						//which would override ours, thus it needs to be called first
						overrideKeyboardFocusWith( focusedPair );
					}

					if( !m_mouseCursorButtonDown )
						focusedPair.widget->setState( States::HighlightedCursor );
					else
						focusedPair.widget->setState( States::HighlightedButtonAndCursor );
					focusedPair.widget->callActionListeners( Action::Highlighted );
				}
				else
				{
					//This call may end up calling focusedPair.widget->getParent()->setState()
					//which would override ours, thus it needs to be called first
					overrideKeyboardFocusWith( focusedPair );

					focusedPair.widget->setState( States::Pressed );
					focusedPair.widget->callActionListeners( Action::Hold );
				}
			}
		}

		m_cursorFocusedPair = focusedPair;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setMouseCursorMoved( Ogre::Vector2 newPosInCanvas )
	{
		const Ogre::Vector2 oldPos = m_mouseCursorPosNdc;
		newPosInCanvas = (newPosInCanvas * m_invCanvasSize2x - Ogre::Vector2::UNIT_SCALE);
		m_mouseCursorPosNdc = newPosInCanvas;

		if( m_allowingScrollGestureWhileButtonDown && (m_allowingScrollAlways ||
			(m_cursorFocusedPair.window && m_cursorFocusedPair.window->hasScroll())) )
		{
			setCancel();
			//setCancel changed m_allowingScrollGestureWhileButtonDown, so restore it
			m_allowingScrollGestureWhileButtonDown = true;
			setScroll( (oldPos - m_mouseCursorPosNdc) * 0.5f * m_canvasSize );
			//^^ setScroll will call updateWidgetsFocusedByCursor if necessary
		}
		else
		{
			updateWidgetsFocusedByCursor();
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setMouseCursorPressed( bool allowScrollGesture, bool alwaysAllowScroll )
	{
		if( m_cursorFocusedPair.widget )
		{
			//This call may end up calling m_cursorFocusedPair.widget->getParent()->setState(),
			//which would override ours, thus it needs to be called first
			overrideKeyboardFocusWith( m_cursorFocusedPair );

			if( m_cursorFocusedPair.widget->isPressable() )
			{
				m_mouseCursorButtonDown = true;

				m_cursorFocusedPair.widget->setState( States::Pressed );
				m_cursorFocusedPair.widget->callActionListeners( Action::Hold );
			}
			else
			{
				m_cursorFocusedPair.widget->setState( States::HighlightedButtonAndCursor );
				m_cursorFocusedPair.widget->callActionListeners( Action::Highlighted );
			}
		}
		else if( m_primaryButtonDown )
		{
			//User clicked outside any widget while keyboard was being hold down. Cancel that key.
			setCancel();
		}

		m_allowingScrollGestureWhileButtonDown = allowScrollGesture;
		m_allowingScrollAlways = alwaysAllowScroll;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setMouseCursorReleased()
	{
		if( m_cursorFocusedPair.widget )
		{
			m_cursorFocusedPair.widget->setState( States::HighlightedCursor );
			if( m_cursorFocusedPair.widget->isPressable() )
				m_cursorFocusedPair.widget->callActionListeners( Action::PrimaryActionPerform );

			if( m_cursorFocusedPair.widget == m_keyboardFocusedPair.widget )
				m_cursorFocusedPair.widget->setState( States::HighlightedButtonAndCursor );
		}
		else if( m_mouseCursorButtonDown )
		{
			setCancel();
		}
		m_mouseCursorButtonDown = false;
		m_allowingScrollGestureWhileButtonDown = false;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setKeyboardPrimaryPressed()
	{
		if( m_keyboardFocusedPair.widget )
		{
			if( m_keyboardFocusedPair.widget->isPressable() )
			{
				m_primaryButtonDown = true;
				m_keyboardFocusedPair.widget->setState( States::Pressed );
				m_keyboardFocusedPair.widget->callActionListeners( Action::Hold );
			}

			overrideCursorFocusWith( m_keyboardFocusedPair );

			scrollToWidget( m_keyboardFocusedPair.widget );
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setKeyboardPrimaryReleased()
	{
		if( m_primaryButtonDown && m_keyboardFocusedPair.widget )
		{
			m_keyboardFocusedPair.widget->setState( States::HighlightedButton );
			if( m_keyboardFocusedPair.widget->isPressable() )
				m_keyboardFocusedPair.widget->callActionListeners( Action::PrimaryActionPerform );

			if( m_cursorFocusedPair.widget == m_keyboardFocusedPair.widget )
				m_keyboardFocusedPair.widget->setState( States::HighlightedButtonAndCursor );

			scrollToWidget( m_keyboardFocusedPair.widget );
		}
		m_primaryButtonDown = false;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setCancel()
	{
		const bool cursorAndKeyboardMatch = m_cursorFocusedPair.widget == m_keyboardFocusedPair.widget;
		States::States newCursorState = States::HighlightedCursor;
		States::States newKeyboardState = States::HighlightedButton;
		if( cursorAndKeyboardMatch )
		{
			newCursorState = States::HighlightedButtonAndCursor;
			newKeyboardState = States::HighlightedButtonAndCursor;
		}

		//Highlight with cursor
		if( m_cursorFocusedPair.widget )
		{
			m_cursorFocusedPair.widget->setState( newCursorState, false );
			if( !cursorAndKeyboardMatch )
				m_cursorFocusedPair.widget->callActionListeners( Action::Cancel );
		}
		m_mouseCursorButtonDown = false;
		m_allowingScrollGestureWhileButtonDown = false;

		//Highlight with keyboard
		if( m_keyboardFocusedPair.widget )
		{
			m_keyboardFocusedPair.widget->setState( newKeyboardState, true );
			if( !cursorAndKeyboardMatch )
				m_keyboardFocusedPair.widget->callActionListeners( Action::Cancel );
		}
		m_primaryButtonDown = false;

		//Cursor and keyboard are highlighting the same widget.
		//Let's make sure we only call the callback once.
		if( cursorAndKeyboardMatch && m_cursorFocusedPair.widget )
			m_cursorFocusedPair.widget->callActionListeners( Action::Cancel );
	}
	//-------------------------------------------------------------------------
	void CrystalManager::updateKeyDirection( Borders::Borders direction )
	{
		if( m_keyboardFocusedPair.widget )
		{
			Widget * crystalgui_nullable nextWidget =
					m_keyboardFocusedPair.widget->m_nextWidget[direction];

			if( nextWidget )
			{
				m_keyboardFocusedPair.widget->setState( States::Idle );
				m_keyboardFocusedPair.widget->callActionListeners( Action::Cancel );

				if( !m_primaryButtonDown || !m_keyboardFocusedPair.widget->isPressable() )
				{
					nextWidget->setState( States::HighlightedButton );
					nextWidget->callActionListeners( Action::Highlighted );
				}
				else
				{
					nextWidget->setState( States::Pressed );
					nextWidget->callActionListeners( Action::Hold );
				}

				m_keyboardFocusedPair.widget = nextWidget;
				overrideCursorFocusWith( m_keyboardFocusedPair );
			}
			else if( !m_keyboardFocusedPair.widget->m_autoSetNextWidget[direction] )
			{
				m_keyboardFocusedPair.widget->_notifyActionKeyMovement( direction );
			}

			scrollToWidget( m_keyboardFocusedPair.widget );
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setKeyDirectionPressed( Borders::Borders direction )
	{
		updateKeyDirection( direction );
		m_keyDirDown = direction;
		m_keyRepeatWaitTimer = 0;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setKeyDirectionReleased( Borders::Borders direction )
	{
		m_keyDirDown = Borders::NumBorders;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::updateAllDerivedTransforms()
	{
		WindowVec::const_iterator itor = m_windows.begin();
		WindowVec::const_iterator end  = m_windows.end();

		while( itor != end )
		{
			(*itor)->_updateDerivedTransformOnly( -Ogre::Vector2::UNIT_SCALE,
												  Ogre::Matrix3::IDENTITY );
			++itor;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setScroll( const Ogre::Vector2 &scrollAmount )
	{
		Window *window = m_cursorFocusedPair.window;
		if( window )
		{
			window->setScrollAnimated( window->getNextScroll() + scrollAmount, true );

			updateAllDerivedTransforms();
			//If is possible the button we were highlighting is no longer behind the cursor
			updateWidgetsFocusedByCursor();
		}
	}
	//-------------------------------------------------------------------------
	Window* CrystalManager::createWindow( Window * crystalgui_nullable parent )
	{
		CRYSTAL_ASSERT( (!parent || parent->isWindow()) &&
						"parent can only be null or a window!" );

		Window *retVal = new Window( this );

		if( !parent )
			m_windows.push_back( retVal );
		else
		{
			retVal->m_parent = parent;
			parent->m_childWindows.push_back( retVal );
			parent->_setParent( retVal );
		}

		retVal->_initialize();

		retVal->setWindowNavigationDirty();
		retVal->setTransformDirty();

		++m_numWidgets;

		if( m_keyboardFocusedPair.window == parent )
		{
			m_keyboardFocusedPair.window = retVal;
			if( m_keyboardFocusedPair.widget )
			{
				m_keyboardFocusedPair.widget->setState( States::Idle );
				m_keyboardFocusedPair.widget->callActionListeners( Action::Cancel );
			}
		}

		return retVal;
	}
	//-------------------------------------------------------------------------
	template <>
	Label * crystalgui_nonnull CrystalManager::createWidget<Label>( Widget * crystalgui_nonnull parent )
	{
		Label *retVal = _createWidget<Label>( parent );
		m_labels.push_back( retVal );
		++m_numLabels;
		return retVal;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::destroyWindow( Window *window )
	{
		if( window == m_cursorFocusedPair.window )
			m_cursorFocusedPair = FocusPair();

		if( window->m_parent )
		{
			WindowVec::iterator itor = std::find( m_windows.begin(), m_windows.end(), window );

			if( itor == m_windows.end() )
			{
				m_logListener->log( "Window does not belong to this CrystalManager! "
									"Double free perhaps?", LogSeverity::Fatal );
			}
			else
				m_windows.erase( itor );
		}

		window->_destroy();
		delete window;

		--m_numWidgets;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::destroyWidget( Widget *widget )
	{
		if( widget == m_cursorFocusedPair.widget )
			m_cursorFocusedPair.widget = 0;

		if( widget->isWindow() )
		{
			CRYSTAL_ASSERT( dynamic_cast<Window*>( widget ) );
			destroyWindow( static_cast<Window*>( widget ) );
		}
		else
		{
			if( widget->isLabel() )
			{
				//We do not update m_numTextGlyphs since it's pointless to shrink it.
				//It will eventually be recalculated anyway
				LabelVec::iterator itor = std::find( m_labels.begin(), m_labels.end(), widget );
				Ogre::efficientVectorRemove( m_labels, itor );
				--m_numLabels;
			}
			widget->_destroy();
			delete widget;
			--m_numWidgets;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::_setAsParentlessWindow( Window *window )
	{
		m_windows.push_back( window );
	}
	//-------------------------------------------------------------------------
	void CrystalManager::setAsParentlessWindow( Window *window )
	{
		if( window->m_parent )
		{
			window->detachFromParent();
			m_windows.push_back( window );
		}
	}
	//-----------------------------------------------------------------------------------
	void CrystalManager::overrideKeyboardFocusWith( const FocusPair &_focusedPair )
	{
		const Widget *cursorWidget = _focusedPair.widget;

		FocusPair focusedPair = _focusedPair;
		focusedPair.widget = focusedPair.widget->getFirstKeyboardNavigableParent();

		//Mouse can steal focus from keyboard and force them to match.
		if( m_keyboardFocusedPair.widget && m_keyboardFocusedPair.widget != focusedPair.widget )
		{
			m_keyboardFocusedPair.widget->setState( States::Idle );
			m_keyboardFocusedPair.widget->callActionListeners( Action::Cancel );
		}
		m_keyboardFocusedPair = focusedPair;
		m_primaryButtonDown = false;

		//If cursor clicked on a widget which is not navigable by the keyboard, then the cursor
		//is setting a different state for that widget. We need to switch the keyboard one
		//to highlighted
		if( focusedPair.widget != cursorWidget )
		{
			m_keyboardFocusedPair.widget->setState( States::HighlightedButton, false );
			m_keyboardFocusedPair.widget->callActionListeners( Action::Highlighted );
		}
	}
	//-----------------------------------------------------------------------------------
	void CrystalManager::overrideCursorFocusWith( const FocusPair &focusedPair )
	{
		//Keyboard can cancel mouse actions, but it won't steal his focus.
		if( m_cursorFocusedPair.widget && m_cursorFocusedPair.widget != focusedPair.widget )
		{
			m_cursorFocusedPair.widget->setState( States::HighlightedCursor, false );
			m_cursorFocusedPair.widget->callActionListeners( Action::Cancel );
		}
		//m_cursorFocusedPair = focusedPair;
		m_mouseCursorButtonDown = false;
	}
	//-----------------------------------------------------------------------------------
	void CrystalManager::checkVertexBufferCapacity()
	{
		CRYSTAL_ASSERT_LOW( m_dirtyLabels.empty() && "updateDirtyLabels has not been called!" );

		bool anyVaoChanged = false;

		if( m_numWidgets * sizeof( Ogre::CbDrawStrip ) > m_indirectBuffer->getNumElements() )
		{
			if( m_indirectBuffer->getMappingState() != Ogre::MS_UNMAPPED )
				m_indirectBuffer->unmap( Ogre::UO_UNMAP_ALL );
			m_vaoManager->destroyIndirectBuffer( m_indirectBuffer );
			const size_t requiredBytes = m_numWidgets * sizeof( Ogre::CbDrawStrip );
			m_indirectBuffer = m_vaoManager->createIndirectBuffer( requiredBytes,
																   Ogre::BT_DYNAMIC_PERSISTENT,
																   0, false );
		}

		{
			//Vertex buffer for most widgets
			const Ogre::uint32 requiredVertexCount =
					static_cast<Ogre::uint32>( (m_numWidgets - m_numLabels) * (6u * 9u) );

			Ogre::VertexBufferPacked *vertexBuffer = m_vao->getBaseVertexBuffer();
			const Ogre::uint32 currVertexCount = vertexBuffer->getNumElements();
			if( requiredVertexCount > currVertexCount )
			{
				const Ogre::uint32 newVertexCount = std::max( requiredVertexCount,
															  currVertexCount +
															  (currVertexCount >> 1u) );
				Ogre::CrystalOgreRenderable::destroyVao( m_vao, m_vaoManager );
				m_vao = Ogre::CrystalOgreRenderable::createVao( newVertexCount, m_vaoManager );

				anyVaoChanged = true;
			}
		}

		{
			//Vertex buffer for text
			const Ogre::uint32 requiredVertexCount =
					static_cast<Ogre::uint32>( m_numTextGlyphs * 6u );

			Ogre::VertexBufferPacked *vertexBuffer = m_textVao->getBaseVertexBuffer();
			const Ogre::uint32 currVertexCount = vertexBuffer->getNumElements();
			if( requiredVertexCount > currVertexCount )
			{
				const Ogre::uint32 newVertexCount = std::max( requiredVertexCount,
															  currVertexCount +
															  (currVertexCount >> 1u) );
				Ogre::CrystalOgreRenderable::destroyVao( m_textVao, m_vaoManager );
				m_textVao = Ogre::CrystalOgreRenderable::createTextVao( newVertexCount, m_vaoManager );
				anyVaoChanged = true;
			}
		}

		if( anyVaoChanged )
		{
			WindowVec::const_iterator itor = m_windows.begin();
			WindowVec::const_iterator end  = m_windows.end();

			while( itor != end )
			{
				(*itor)->broadcastNewVao( m_vao, m_textVao );
				++itor;
			}
		}

	}
	//-------------------------------------------------------------------------
	template <typename T>
	void CrystalManager::autosetNavigation( const std::vector<T> &container,
											size_t start, size_t numWidgets )
	{
		CRYSTAL_ASSERT( start + numWidgets <= container.size() );

		typename std::vector<T>::const_iterator itor = container.begin() + start;
		typename std::vector<T>::const_iterator end  = container.begin() + start + numWidgets;

		//Remove existing links
		while( itor != end )
		{
			Widget *widget = *itor;
			for( size_t i=0; i<4u; ++i )
			{
				if( widget->isKeyboardNavigable() && widget->m_autoSetNextWidget[i] )
					widget->setNextWidget( 0, static_cast<Borders::Borders>( i ) );
			}
			++itor;
		}

		//Search for them again
		itor = container.begin() + start;

		while( itor != end )
		{
			Widget *widget = *itor;

			if( widget->isKeyboardNavigable() )
			{
				Widget *closestSiblings[Borders::NumBorders] = { 0, 0, 0, 0 };
				float closestSiblingDistances[Borders::NumBorders] =
				{
					std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max(),
					std::numeric_limits<float>::max()
				};

				typename std::vector<T>::const_iterator it2 = itor + 1u;
				while( it2 != end )
				{
					Widget *widget2 = *it2;

					if( widget2->isKeyboardNavigable() )
					{
						const Ogre::Vector2 cornerToCorner[4] =
						{
							widget2->m_position -
							widget->m_position,

							Ogre::Vector2( widget2->getRight(), widget2->m_position.y ) -
							Ogre::Vector2( widget->getRight(), widget->m_position.y ),

							Ogre::Vector2( widget2->m_position.x, widget2->getBottom() ) -
							Ogre::Vector2( widget->m_position.x, widget->getBottom() ),

							Ogre::Vector2( widget2->getRight(), widget2->getBottom() ) -
							Ogre::Vector2( widget->getRight(), widget->getBottom() ),
						};

						for( size_t i=0; i<4u; ++i )
						{
							Ogre::Vector2 dirTo = cornerToCorner[i];

							const float dirLength = dirTo.normalise();

							const float cosAngle( dirTo.dotProduct( Ogre::Vector2::UNIT_X ) );

							if( dirLength < closestSiblingDistances[Borders::Right] &&
								cosAngle >= cosf( Ogre::Degree( 45.0f ).valueRadians() ) )
							{
								closestSiblings[Borders::Right] = widget2;
								closestSiblingDistances[Borders::Right] = dirLength;
							}

							if( dirLength < closestSiblingDistances[Borders::Left] &&
								cosAngle <= cosf( Ogre::Degree( 135.0f ).valueRadians() ) )
							{
								closestSiblings[Borders::Left] = widget2;
								closestSiblingDistances[Borders::Left] = dirLength;
							}

							if( cosAngle <= cosf( Ogre::Degree( 45.0f ).valueRadians() ) &&
								cosAngle >= cosf( Ogre::Degree( 135.0f ).valueRadians() ) )
							{
								float crossProduct = dirTo.crossProduct( Ogre::Vector2::UNIT_X );

								if( crossProduct >= 0.0f )
								{
									if( dirLength < closestSiblingDistances[Borders::Top] )
									{
										closestSiblings[Borders::Top] = widget2;
										closestSiblingDistances[Borders::Top] = dirLength;
									}
								}
								else
								{
									if( dirLength < closestSiblingDistances[Borders::Bottom] )
									{
										closestSiblings[Borders::Bottom] = widget2;
										closestSiblingDistances[Borders::Bottom] = dirLength;
									}
								}
							}
						}
					}

					++it2;
				}

				for( size_t i=0; i<4u; ++i )
				{
					if( widget->m_autoSetNextWidget[i] && !widget->m_nextWidget[i] )
						widget->setNextWidget( closestSiblings[i], static_cast<Borders::Borders>( i ) );
				}
			}

			++itor;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::autosetNavigation( Window *window )
	{
		if( window->m_widgetNavigationDirty )
		{
			//Update the widgets from this 'window'
			autosetNavigation( window->m_children, 0, window->m_numWidgets );
			window->m_widgetNavigationDirty = false;
		}

		if( window->m_windowNavigationDirty )
		{
			//Update the widgets of the children windows from this 'window'
			autosetNavigation( window->m_childWindows, 0, window->m_childWindows.size() );
			window->m_windowNavigationDirty = false;
		}

		if( window->m_childrenNavigationDirty )
		{
			//Our windows' window are dirty
			WindowVec::const_iterator itor = window->m_childWindows.begin();
			WindowVec::const_iterator end  = window->m_childWindows.end();

			while( itor != end )
				autosetNavigation( *itor++ );

			window->m_childrenNavigationDirty = false;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::updateDirtyLabels()
	{
		bool recalculateNumGlyphs = false;
		LabelVec::const_iterator itor = m_dirtyLabels.begin();
		LabelVec::const_iterator end  = m_dirtyLabels.end();

		while( itor != end )
		{
			recalculateNumGlyphs |= (*itor)->_updateDirtyGlyphs();
			++itor;
		}

		m_dirtyLabels.clear();

		if( recalculateNumGlyphs )
		{
			m_numTextGlyphs = 0;
			itor = m_labels.begin();
			end  = m_labels.end();

			while( itor != end )
			{
				m_numTextGlyphs += (*itor)->getMaxNumGlyphs();
				++itor;
			}
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::autosetNavigation()
	{
		updateDirtyLabels();
		checkVertexBufferCapacity();

		if( m_windowNavigationDirty )
		{
			WindowVec::const_iterator itor = m_windows.begin();
			WindowVec::const_iterator end  = m_windows.end();

			while( itor != end )
				autosetNavigation( *itor++ );

			m_windowNavigationDirty = false;
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::_setWindowNavigationDirty()
	{
		m_windowNavigationDirty = true;
	}
	//-------------------------------------------------------------------------
	void CrystalManager::_addDirtyLabel( Label *label )
	{
		m_dirtyLabels.push_back( label );
	}
	//-------------------------------------------------------------------------
	void CrystalManager::scrollToWidget( Widget *widget )
	{
		//Only scroll if the immediate parent is a window.
		Window *parentWindow = widget->getParent()->getAsWindow();
		if( parentWindow )
		{
			//Ensure the widget is up to date. The window is implicitly going to be updated.
			//widget->updateDerivedTransformFromParent();

			const Ogre::Vector2 currentScroll = parentWindow->getCurrentScroll();

			const Ogre::Vector2 parentTL = parentWindow->getTopLeftAfterClipping();
			const Ogre::Vector2 parentBR = parentWindow->getBottomRightAfterClipping();
			const Ogre::Vector2 widgetTL = parentTL - currentScroll + widget->getLocalTopLeft();
			const Ogre::Vector2 widgetBR = parentTL - currentScroll + widget->getLocalBottomRight();

			Ogre::Vector2 scrollOffset( Ogre::Vector2::ZERO );

			if( widgetBR.y > parentBR.y )
				scrollOffset.y = widgetBR.y - parentBR.y;
			if( widgetTL.y < parentTL.y )
				scrollOffset.y = widgetTL.y - parentTL.y;
			if( widgetBR.x > parentBR.x )
				scrollOffset.x = widgetBR.x - parentBR.x;
			if( widgetTL.x < parentTL.x )
				scrollOffset.x = widgetTL.x - parentTL.x;

			if( scrollOffset != Ogre::Vector2::ZERO )
				parentWindow->setScrollAnimated( currentScroll + scrollOffset, false );
		}
	}
	//-------------------------------------------------------------------------
	void CrystalManager::update( float timeSinceLast )
	{
		autosetNavigation();

		if( m_keyboardFocusedPair.widget && !m_keyboardFocusedPair.widget->isKeyboardNavigable() )
			m_keyboardFocusedPair.widget = 0;
		if( m_cursorFocusedPair.widget &&
			(m_cursorFocusedPair.widget->isHidden() || m_cursorFocusedPair.widget->isDisabled()) )
		{
			m_cursorFocusedPair = m_keyboardFocusedPair;
		}

		if( m_keyboardFocusedPair.window && !m_keyboardFocusedPair.widget )
		{
			m_keyboardFocusedPair.widget = m_keyboardFocusedPair.window->getDefaultWidget();
			m_keyboardFocusedPair.widget->setState( States::HighlightedButton );
			m_keyboardFocusedPair.widget->callActionListeners( Action::Highlighted );

			scrollToWidget( m_keyboardFocusedPair.widget );
		}

		if( m_keyDirDown != Borders::NumBorders )
		{
			while( m_keyRepeatWaitTimer >= m_keyRepeatDelay )
			{
				updateKeyDirection( m_keyDirDown );
				m_keyRepeatWaitTimer -= m_timeDelayPerKeyStroke;
			}

			m_keyRepeatWaitTimer += timeSinceLast;
		}

		bool cursorFocusDirty = false;

		WindowVec::const_iterator itor = m_windows.begin();
		WindowVec::const_iterator end  = m_windows.end();

		while( itor != end )
		{
			cursorFocusDirty |= (*itor)->update( timeSinceLast );
			++itor;
		}

		if( cursorFocusDirty )
		{
			//Scroll changed, cursor may now be highlighting a different widget
			updateAllDerivedTransforms();
			updateWidgetsFocusedByCursor();
		}

		m_shaperManager->updateGpuBuffers();
	}
	//-------------------------------------------------------------------------
	void CrystalManager::prepareRenderCommands()
	{
		Ogre::VertexBufferPacked *vertexBuffer = m_vao->getBaseVertexBuffer();
		Ogre::VertexBufferPacked *vertexBufferText = m_textVao->getBaseVertexBuffer();

		UiVertex *vertex = reinterpret_cast<UiVertex*>(
							   vertexBuffer->map( 0, vertexBuffer->getNumElements() ) );
		UiVertex *startOffset = vertex;

		GlyphVertex *vertexText = reinterpret_cast<GlyphVertex*>(
									  vertexBufferText->map( 0, vertexBufferText->getNumElements() ) );
		GlyphVertex *startOffsetText = vertexText;

		WindowVec::const_iterator itor = m_windows.begin();
		WindowVec::const_iterator end  = m_windows.end();

		while( itor != end )
		{
			(*itor)->_fillBuffersAndCommands( &vertex, &vertexText,
											  -Ogre::Vector2::UNIT_SCALE,
											  Ogre::Vector2::ZERO,
											  Ogre::Matrix3::IDENTITY );
			++itor;
		}

		const size_t elementsWritten = vertex - startOffset;
		const size_t elementsWrittenText = vertexText - startOffsetText;
		CRYSTAL_ASSERT( elementsWritten <= vertexBuffer->getNumElements() );
		CRYSTAL_ASSERT( elementsWrittenText <= vertexBufferText->getNumElements() );
		vertexBuffer->unmap( Ogre::UO_KEEP_PERSISTENT, 0u, elementsWritten );
		vertexBufferText->unmap( Ogre::UO_KEEP_PERSISTENT, 0u, elementsWrittenText );
	}
	//-------------------------------------------------------------------------
	void CrystalManager::render()
	{
		ApiEncapsulatedObjects apiObjects;

		Ogre::HlmsManager *hlmsManager = m_root->getHlmsManager();

		Ogre::Hlms *hlms = hlmsManager->getHlms( Ogre::HLMS_UNLIT );
		CRYSTAL_ASSERT_HIGH( dynamic_cast<Ogre::HlmsCrystal*>( hlms ) );
		Ogre::HlmsCrystal *hlmsCrystal = static_cast<Ogre::HlmsCrystal*>( hlms );

		apiObjects.lastHlmsCache = &c_dummyCache;

		Ogre::HlmsCache passCache = hlms->preparePassHash( 0, false, false, m_sceneManager );
		apiObjects.passCache = &passCache;
		apiObjects.hlms = hlmsCrystal;
		apiObjects.lastVaoName = 0;
		apiObjects.commandBuffer = m_commandBuffer;
		apiObjects.indirectBuffer = m_indirectBuffer;
		if( m_vaoManager->supportsIndirectBuffers() )
		{
			apiObjects.indirectDraw = reinterpret_cast<uint8_t*>(
										  m_indirectBuffer->map( 0,
																 m_indirectBuffer->getNumElements() ) );
		}
		else
		{
			apiObjects.indirectDraw = reinterpret_cast<uint8_t*>( m_indirectBuffer->getSwBufferPtr() );
		}
		apiObjects.startIndirectDraw = apiObjects.indirectDraw;
		apiObjects.lastDatablock = 0;
		apiObjects.baseInstanceAndIndirectBuffers = 0;
		if( m_vaoManager->supportsIndirectBuffers() )
			apiObjects.baseInstanceAndIndirectBuffers = 2;
		else if( m_vaoManager->supportsBaseInstance() )
			apiObjects.baseInstanceAndIndirectBuffers = 1;
		apiObjects.drawCmd = 0;
		apiObjects.drawCountPtr = 0;
		apiObjects.primCount = 0;
		apiObjects.accumPrimCount[0] = m_vao->getBaseVertexBuffer()->_getFinalBufferStart();
		apiObjects.accumPrimCount[1] = m_textVao->getBaseVertexBuffer()->_getFinalBufferStart();

		WindowVec::const_iterator itor = m_windows.begin();
		WindowVec::const_iterator end  = m_windows.end();

		while( itor != end )
		{
			(*itor)->addCommands( apiObjects );
			++itor;
		}

		if( m_vaoManager->supportsIndirectBuffers() )
			m_indirectBuffer->unmap( Ogre::UO_KEEP_PERSISTENT );

		hlms->preCommandBufferExecution( m_commandBuffer );
		m_commandBuffer->execute();
		hlms->postCommandBufferExecution( m_commandBuffer );
	}
}
