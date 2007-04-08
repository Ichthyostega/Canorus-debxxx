/*!
	Copyright (c) 2006-2007, Matevž Jekovec, Canorus development team
	All Rights Reserved. See AUTHORS for a complete list of authors.
	
	Licensed under the GNU GENERAL PUBLIC LICENSE. See LICENSE.GPL for details.
*/

#include <QGridLayout>
#include <QScrollBar>
#include <QPainter>
#include <QBrush>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPalette>
#include <QColor>
#include <QTimer>

#include <math.h>	// neded for square root in animated scrolls/zoom

#include <iostream>

#include "widgets/scoreviewport.h"
#include "drawable/drawable.h"
#include "drawable/drawablecontext.h"
#include "drawable/drawablemuselement.h"
#include "drawable/drawablestaff.h"
#include "drawable/drawablenote.h"
#include "drawable/drawableaccidental.h"
#include "core/muselement.h"
#include "core/context.h"
#include "interface/engraver.h"
#include "core/staff.h"
#include "core/note.h"
#include "core/document.h"
#include "core/sheet.h"

const int CAScoreViewPort::RIGHT_EXTRA_SPACE = 100;	// Gives some space after the music so you're able to insert music elements after the last element
const int CAScoreViewPort::BOTTOM_EXTRA_SPACE = 30; // Gives some space after the music so you're able to insert new contexts below the last context
const int CAScoreViewPort::ANIMATION_STEPS = 7;

CAScoreViewPort::CAScoreViewPort(CASheet *sheet, QWidget *parent) : CAViewPort(parent) {
	_viewPortType = CAViewPort::ScoreViewPort;
	
	_sheet = sheet;
	_worldX = _worldY = 0;
	_worldW = _worldH = 0;
	_zoom = 1.0;
	_holdRepaint = false;
	_hScrollBarDeadLock = false;
	_vScrollBarDeadLock = false;
	_checkScrollBarsDeadLock = false;
	_playing = false;
	_currentContext = 0;

	//setup animation stuff
	_animationTimer = new QTimer();
	_animationTimer->setInterval(50);
	connect(_animationTimer, SIGNAL(timeout()), this, SLOT(on__animationTimer_timeout()));
	
	//set the shadow note
	_shadowNoteVisible = false;
	_shadowNoteVisibleOnLeave = false;
	_shadowNoteAccs = 0;
	_drawShadowNoteAccs = false;
	
	//setup the virtual canvas
	_canvas = new QWidget(this);
	setMouseTracking(true);
	_canvas->setMouseTracking(true);
	_backgroundBrush = QBrush(QColor(255, 255, 240));
	_repaintArea = 0;
	
	//setup the scrollbars
	_vScrollBar = new QScrollBar(Qt::Vertical, this);
	_hScrollBar = new QScrollBar(Qt::Horizontal, this);
	_vScrollBar->setMinimum(0);
	_hScrollBar->setMinimum(0);
	_vScrollBar->setTracking(true); //trigger valueChanged() when dragging the slider, not only releasing it
	_hScrollBar->setTracking(true);
	_vScrollBar->hide();
	_hScrollBar->hide();
	_scrollBarVisible = ScrollBarShowIfNeeded;
	_allowManualScroll = true;
	
	connect(_hScrollBar, SIGNAL(valueChanged(int)), this, SLOT(HScrollBarEvent(int)));
	connect(_vScrollBar, SIGNAL(valueChanged(int)), this, SLOT(VScrollBarEvent(int)));
	
	// setup layout
	_layout = new QGridLayout(this);
	_layout->setMargin(2);
	_layout->setSpacing(2);
	_drawBorder = false;
	_layout->addWidget(_canvas, 0, 0);
	_layout->addWidget(_vScrollBar, 0, 1);
	_layout->addWidget(_hScrollBar, 1, 0);
	
	_oldWorldW = 0; _oldWorldH = 0;
	
}

CAScoreViewPort::~CAScoreViewPort() {
	_drawableMList.clear(true);	//clears all the elements and delete its drawable contents too as autoDelete is true
	_drawableCList.clear(true);	//clears all the elements and delete its drawable contents too as autoDelete is true
	
	_animationTimer->disconnect();
	_animationTimer->stop();
	delete _animationTimer;
	
	_hScrollBar->disconnect();
	_vScrollBar->disconnect();
	this->disconnect();
}

void CAScoreViewPort::on__animationTimer_timeout() {
	_animationStep++;
	
	float newZoom = _zoom + (_targetZoom - _zoom) * sqrt(((float)_animationStep)/ANIMATION_STEPS);
	int newWorldX = (int)(_worldX + (_targetWorldX - _worldX) * sqrt(((float)_animationStep)/ANIMATION_STEPS));
	int newWorldY = (int)(_worldY + (_targetWorldY - _worldY) * sqrt(((float)_animationStep)/ANIMATION_STEPS));
	int newWorldW = (int)(drawableWidth() / newZoom);
	int newWorldH = (int)(drawableHeight() / newZoom);
	
	setWorldCoords(newWorldX, newWorldY, newWorldW, newWorldH);
	
	if (_animationStep==ANIMATION_STEPS)
		_animationTimer->stop();
	
	repaint();
}

CAScoreViewPort *CAScoreViewPort::clone() {
	CAScoreViewPort *v = new CAScoreViewPort(_sheet, _parent);
	
	v->importElements(&_drawableMList, &_drawableCList);
	
	return v;
}

CAScoreViewPort *CAScoreViewPort::clone(QWidget *parent) {
	CAScoreViewPort *v = new CAScoreViewPort(_sheet, parent);
	
	v->importElements(&_drawableMList, &_drawableCList);
	
	return v;
}

/*!
	Adds a drawable music element \a elt to the score viewport and selects it, if \a select is true.
*/

void CAScoreViewPort::addMElement(CADrawableMusElement *elt, bool select) {
	_drawableMList.addElement(elt);
	if (select) {
		_selection.clear();
		_selection << elt;
	}
	
	elt->drawableContext()->addMElement(elt);
}

/*!
	Adds a drawable music element \a elt to the score viewport and selects it, if \a select is true.
*/
void CAScoreViewPort::addCElement(CADrawableContext *elt, bool select) {
	_drawableCList.addElement(elt);
	
	if (select)
		setCurrentContext(elt);
	
	if (elt->drawableContextType() == CADrawableContext::DrawableStaff) {
		_shadowNote << new CANote(CANote::Whole, 0, 0, 0, 0);
		_shadowNote.back()->setVoice(((CADrawableStaff*)elt)->staff()->voiceAt(0));
		_shadowDrawableNote << new CADrawableNote(_shadowNote.back(), 0, 0, 0, true);
		_shadowDrawableNote.back()->setDrawableContext(elt);
	}
}

/*!
	Selects the drawable context of the given abstract context.
	If there are multiple drawable elements representing a single abstract element, selects the first one.
	
	Returns a pointer to the drawable instance of the given context or 0 if the context was not found.
	
	\sa selectMElement(CAMusElement*)
*/
CADrawableContext *CAScoreViewPort::selectContext(CAContext *context) {
	if (!context) {
		setCurrentContext(0);
		return false;
	}
	
	for (int i=0; i<_drawableCList.size(); i++) {
		CAContext *c = ((CADrawableContext*)_drawableCList.at(i))->context();
		if (c == context) {
			setCurrentContext((CADrawableContext*)_drawableCList.at(i));
			return (CADrawableContext*)_drawableCList.at(i);
		}
	}
	
	return 0;
}

/*!
	Selects a drawable context element at the given coordinates, if it exists.
	Returns a pointer to its abstract context element.
	If multiple elements exist at the same coordinates, they are selected one by another if you click at the same coordinates multiple times.
	If no elements are present at the coordinates, clear the selection.
*/
CADrawableContext* CAScoreViewPort::selectCElement(int x, int y) {
	QList<CADrawable *> l = _drawableCList.findInRange(x,y);
	
	if (l.size()!=0) {
		setCurrentContext((CADrawableContext*)l.front());
	} else
		setCurrentContext(0);
	
	return currentContext();
}

/*!
	Selects a drawable music element at the given coordinates, if it exists.
	Returns a pointer to its abstract music element.
	If multiple elements exist at the same coordinates, they are selected one by another if you click at the same coordinates multiple times.
	If no elements are present at the coordinates, clear the selection.
*/
CAMusElement* CAScoreViewPort::selectMElement(int x, int y) {
	QList<CADrawable *> l = _drawableMList.findInRange(x,y);
	for (int i=0; i<l.size(); i++)
		if (!((CADrawableMusElement*)l[i])->isSelectable())
			l.removeAt(i);
	
	if (l.size() != 0) { //multiple elements can share the same coordinates
		int idx;
		if ( (_selection.size() != 1) || ((idx = l.indexOf(_selection.front())) == -1) ) {
			_selection.clear();
			_selection << (CADrawableMusElement*)l.at(0);	//if the previous selection was not a single element or if the new list doesn't contain the selection, add the first element in the available list to the selection
		} else {
			_selection.clear();
			_selection << (CADrawableMusElement*)l.at((++idx < l.size()) ? idx : 0); //if there are two or more elements with the same coordinates, select the next one (behind it). This way, you can click multiple times on the same place and you'll always select the other element.
		}
		
		setCurrentContext(((CADrawableMusElement*)_selection.front())->drawableContext());
		return ((CADrawableMusElement*)_selection.front())->musElement();
	} else {
		if (_selection.size() != 0) {
			_selection.clear();
		}
		
		return 0;
	}
}

/*!
	Selects the drawable music element of the given abstract music element.
	If there are multiple drawable elements representing a single abstract element, selects the first one.
	
	Returns a pointer to the drawable instance of the given music element or 0 if the music element was not found.
	
	\sa selectCElement(CAContext*)
*/
CADrawableMusElement* CAScoreViewPort::selectMElement(CAMusElement *elt) {
	_selection.clear();
	
	for (int i=0; i<_drawableMList.size(); i++)
		if (((CADrawableMusElement*)(_drawableMList.at(i)))->musElement() == elt) {
			_selection << (CADrawableMusElement*)_drawableMList.at(i);
			return (CADrawableMusElement*)_drawableMList.at(i);
		}
	
	return 0;	
}

/*!
	Removes a drawable music element at the given coordinates \a x and \a y, if it exists.
	Returns the pointer of the abstract music element, if the element was found and deleted.
	\warning This function only deletes the CADrawable part of the object. You still need to delete the abstract part (the pointer returned)!
*/
CAMusElement *CAScoreViewPort::removeMElement(int x, int y) {
	CADrawableMusElement *elt = (CADrawableMusElement*)_drawableMList.removeElement(x,y,false);
	if (elt) {
		if (elt->drawableMusElementType() == CADrawableMusElement::DrawableClef)
			((CADrawableStaff*)elt->drawableContext())->removeClef((CADrawableClef*)elt);
		else if (elt->drawableMusElementType() == CADrawableMusElement::DrawableKeySignature)
			((CADrawableStaff*)elt->drawableContext())->removeKeySignature((CADrawableKeySignature*)elt);
		else if (elt->drawableMusElementType() == CADrawableMusElement::DrawableTimeSignature)
			((CADrawableStaff*)elt->drawableContext())->removeTimeSignature((CADrawableTimeSignature*)elt);
		
		elt->drawableContext()->removeMElement(elt);
		CAMusElement *mElt = elt->musElement();
		delete elt;	//delete drawable instance

		return mElt;
	}
	
	return 0;
}

void CAScoreViewPort::importElements(CAKDTree *drawableMList, CAKDTree *drawableCList)
{
	for (int i=0; i<drawableCList->size(); i++)
		addCElement(((CADrawableContext*)drawableCList->at(i))->clone());
	for (int i=0; i<drawableMList->size(); i++)
	{
		CADrawableContext* target;
		int idx = drawableCList->list().indexOf(((CADrawableMusElement*)drawableMList->at(i))->drawableContext());
		if(idx == -1)
		{
			printf("Error!! Music element %p is not in its context!\n", drawableMList->at(i));
			target = 0;
		} else 
			target = (CADrawableContext*)_drawableCList.at(idx);
		addMElement(((CADrawableMusElement*)drawableMList->at(i))->clone(target));
	}
}

void CAScoreViewPort::importMElements(CAKDTree *elts) {
	for (int i=0; i<elts->size(); i++)
		addMElement((CADrawableMusElement*)elts->at(i)->clone());
}

void CAScoreViewPort::importCElements(CAKDTree *elts) {
	for (int i=0; i<elts->size(); i++)
		addCElement((CADrawableContext*)elts->at(i)->clone());
}

/*!
	Returns a pointer to the nearest drawable music element left of the current coordinates with the largest startTime.
	Drawable elements left borders are taken into account. 
	Returns the nearest element in the current context only, if currentContext is true (default).
*/
CADrawableMusElement *CAScoreViewPort::nearestLeftElement(int x, int y, bool currentContext) {
	CADrawableMusElement *elt;
	return ( (elt = (CADrawableMusElement*)_drawableMList.findNearestLeft(x, true, currentContext?_currentContext:0))?
	         elt : 0);
}

/*!
	Returns a pointer to the nearest drawable music element left of the current coordinates with the
	largest startTime in the given voice.
*/
CADrawableMusElement *CAScoreViewPort::nearestLeftElement(int x, int y, CAVoice *voice) {
	CADrawableMusElement *elt;
	return ( (elt = (CADrawableMusElement*)_drawableMList.findNearestLeft(x, true, _currentContext, voice))?
	         elt : 0);
}

/*!
	Returns a pointer to the nearest drawable music element right of the current coordinates with the largest startTime.
	Drawable elements left borders are taken into account. 
	Returns the nearest element in the current context only, if currentContext is true (default).
*/
CADrawableMusElement *CAScoreViewPort::nearestRightElement(int x, int y, bool currentContext) {
	CADrawableMusElement *elt;
	return ( (elt = (CADrawableMusElement*)_drawableMList.findNearestRight(x, true, currentContext?_currentContext:0))?
	         elt : 0);
}

/*!
	Returns a pointer to the nearest drawable music element right of the current coordinates with the
	largest startTime in the given voice.
*/
CADrawableMusElement *CAScoreViewPort::nearestRightElement(int x, int y, CAVoice *voice) {
	CADrawableMusElement *elt;
	return ( (elt = (CADrawableMusElement*)_drawableMList.findNearestRight(x, true, _currentContext, voice))?
	         elt : 0);
}

/*!
	Calculates the logical time at the given coordinates \a x and \a y.
*/
int CAScoreViewPort::calculateTime(int x, int y) {
	CADrawableMusElement *left = (CADrawableMusElement *)_drawableMList.findNearestLeft(x, true);
	CADrawableMusElement *right = (CADrawableMusElement *)_drawableMList.findNearestRight(x, true);
	
	if (left)	//the user clicked right of the element - return the nearest left element end time
		return left->musElement()->timeStart() + left->musElement()->timeLength();
	else if (right)	//the user clicked left of the element - return the nearest right element start time
		return right->musElement()->timeStart();
	else	//no elements found in the score at all - return 0
		return 0;
}

/*!
	If the given coordinates hit any of the contexts, returns that context.
*/
CAContext *CAScoreViewPort::contextCollision(int x, int y) {
	QList<CADrawable*> l = _drawableCList.findInRange(x, y, 0, 0);
	if (l.size() == 0) {
		return 0;
	} else {
		CAContext *context = ((CADrawableContext*)l.front())->context(); 
		return context;
	}
}

void CAScoreViewPort::rebuild() {
	//clear the shadow notes
	for (int i=0; i<_shadowNote.size(); i++) {
		delete _shadowNote[i];
		delete _shadowDrawableNote[i];
	}
	_shadowNote.clear();
	_shadowDrawableNote.clear();

	QList<CAMusElement*> musElementSelection;
	for (int i=0; i<_selection.size(); i++)
		musElementSelection << _selection[i]->musElement();
	
	_selection.clear();
	
	_drawableMList.clear(true);
	int contextIdx = (_currentContext ? _drawableCList.list().indexOf(_currentContext) : -1);	//remember the index of last used context
	_drawableCList.clear(true);
		
	CAEngraver::reposit(this);
	
	if (contextIdx != -1)	//restore the last used context
		setCurrentContext((CADrawableContext*)((_drawableCList.size() > contextIdx)?_drawableCList.list().at(contextIdx):0));
	else
		setCurrentContext(0);
	
	addToSelection(musElementSelection);
	
	checkScrollBars();
	calculateShadowNoteCoords();
}

/*!
	Sets the world Top-Left X coordinate of the viewport. Animates the scroll, if \a animate is True.
	If \a force is True, sets the value despite the potential illegal value (like negative coordinates).
	
	\warning Repaint is not done automatically!
*/
void CAScoreViewPort::setWorldX(int x, bool animate, bool force) {
	if (!force) {
		int maxX = (getMaxXExtended(_drawableMList) > getMaxXExtended(_drawableCList))?getMaxXExtended(_drawableMList) : getMaxXExtended(_drawableCList);
		if (x > maxX - _worldW)
			x = maxX - _worldW;
		if (x < 0)
			x = 0;
	}
	
	if (animate) {
		_targetWorldX = x;
		_targetWorldY = _worldY;
		_targetZoom = _zoom;
		startAnimationTimer();
		return;
	}

	_oldWorldX = _worldX;
	_worldX = x;
	_hScrollBarDeadLock = true;
	_hScrollBar->setValue(x);
	_hScrollBarDeadLock = false;

	checkScrollBars();
	calculateShadowNoteCoords();
}

/*!
	Sets the world Top-Left Y coordinate of the viewport. Animates the scroll, if \a animate is True.
	If \a force is True, sets the value despite the potential illegal value (like negative coordinates).
	
	\warning Repaint is not done automatically!
*/
void CAScoreViewPort::setWorldY(int y, bool animate, bool force) {
	if (!force) {
		int maxY = getMaxYExtended(_drawableMList) > getMaxYExtended(_drawableCList)?getMaxYExtended(_drawableMList) : getMaxYExtended(_drawableCList);
		if (y > maxY - _worldH)
			y = maxY - _worldH;
		if (y < 0)
			y = 0;
	}
	
	if (animate) {
		_targetWorldX = _worldX;
		_targetWorldY = y;
		_targetZoom = _zoom;
		startAnimationTimer();
		return;
	}

	_oldWorldY = _worldY;
	_worldY = y;
	_vScrollBarDeadLock = true;
	_vScrollBar->setValue(y);
	_vScrollBarDeadLock = false;
	
	checkScrollBars();
	calculateShadowNoteCoords();
}

/*!
	Sets the world width of the viewport.
	If \a force is True, sets the value despite the potential illegal value (like negative coordinates).
	
	\warning Repaint is not done automatically!
*/
void CAScoreViewPort::setWorldWidth(int w, bool force) {
	if (!force) {
		if (w < 1) return;
	}
	
	_oldWorldW = _worldW;
	_worldW = w;
	
	int scrollMax;
	if ((scrollMax = ((getMaxXExtended(_drawableMList) > getMaxXExtended(_drawableCList))?getMaxXExtended(_drawableMList):getMaxXExtended(_drawableCList)) - _worldW) >= 0) {
		if (scrollMax < _worldX)	//if you resize the widget at a large zoom level and if the getMax border has been reached
			setWorldX(scrollMax);	//scroll the view away from the border
			
		_hScrollBarDeadLock = true;
		_hScrollBar->setMaximum(scrollMax);
		_hScrollBar->setPageStep(_worldW);
		_hScrollBarDeadLock = false;
	}
	
	_zoom = ((float)drawableWidth() / _worldW);

	checkScrollBars();
}

/*!
	Sets the world height of the viewport.
	If \a force is True, sets the value despite the potential illegal value (like negative coordinates).
	
	\warning Repaint is not done automatically!
*/
void CAScoreViewPort::setWorldHeight(int h, bool force) {
	if (!force) {
		if (h < 1) return;
	}
	
	_oldWorldH = _worldH;
	_worldH = h;

	int scrollMax;
	if ((scrollMax = ((getMaxYExtended(_drawableMList) > getMaxYExtended(_drawableCList))?getMaxYExtended(_drawableMList):getMaxYExtended(_drawableCList)) - _worldH) >= 0) {
		if (scrollMax < _worldY)	//if you resize the widget at a large zoom level and if the getMax border has been reached
			setWorldY(scrollMax);	//scroll the view away from the border
		
		_vScrollBarDeadLock = true;
		_vScrollBar->setMaximum(scrollMax);
		_vScrollBar->setPageStep(_worldH);
		_vScrollBarDeadLock = false;
	}

	_zoom = ((float)drawableHeight() / _worldH);

	checkScrollBars();
}

/*!
	Sets the world coordinates of the viewport to the given rectangle \a coords.
	This is an overloaded member function, provided for convenience.
	
	\warning Repaint is not done automatically!
*/
void CAScoreViewPort::setWorldCoords(QRect coords, bool animate, bool force) {
	_checkScrollBarsDeadLock = true;

	if (!drawableWidth() && !drawableHeight())
		return;
	
	float scale = (float)drawableWidth() / drawableHeight();	//always keep the world rectangle area in the same scale as the actual width/height of the drawable canvas
	if (coords.height()) {	//avoid division by zero
		if ((float)coords.width() / coords.height() > scale)
			coords.setHeight( qRound(coords.width() / scale) );
		else
			coords.setWidth( qRound(coords.height() * scale) );
	} else
		coords.setHeight( qRound(coords.width() / scale) );
		
	
	setWorldWidth(coords.width(), force);
	setWorldHeight(coords.height(), force);
	setWorldX(coords.x(), animate, force);
	setWorldY(coords.y(), animate, force);
	_checkScrollBarsDeadLock = false;

	checkScrollBars();
}

/*!
	\fn void CAScoreViewPort::setWorldCoords(int x, int y, int w, int h, bool animate, bool force)
	Sets the world coordinates of the viewport.
	This is an overloaded member function, provided for convenience.
	
	\warning Repaint is not done automatically!
	
	\param x Top-left X coordinate of the new viewport area in absolute world units.
	\param y Top-left Y coordinate of the new viewport area in absolute world units.
	\param w Width of the new viewport area in absolute world units.
	\param h Height of the new viewport area in absolute world units.
	\param animate Use animated scroll.
	\param force Use the given world units despite their illegal values (like negative coordinates etc.).
*/

void CAScoreViewPort::zoomToSelection(bool animate, bool force) {
	if (!_selection.size())
		return;
	
	QRect rect;
	
	rect.setX(_selection[0]->xPos()); rect.setY(_selection[0]->yPos());
	rect.setWidth(_selection[0]->width()); rect.setHeight(_selection[0]->height());
	for (int i=1; i<_selection.size(); i++) {
		if (_selection[i]->xPos() < rect.x())
			rect.setX(_selection[i]->xPos());
		if (_selection[i]->yPos() < rect.y())
			rect.setY(_selection[i]->yPos());
		if (_selection[i]->xPos() + _selection[i]->width() > rect.x() + rect.width())
			rect.setWidth(_selection[i]->xPos() + _selection[i]->width() - rect.x());
		if (_selection[i]->yPos() + _selection[i]->height() > rect.y() + rect.height())
			rect.setHeight(_selection[i]->yPos() + _selection[i]->height() - rect.y());
	}
	
	setWorldCoords(rect, animate, force);
}

void CAScoreViewPort::zoomToWidth(bool animate, bool force) {
	int maxX = (getMaxXExtended(_drawableCList)>getMaxXExtended(_drawableMList))?getMaxXExtended(_drawableCList):getMaxXExtended(_drawableMList);
	setWorldCoords(0,0,maxX,0,animate,force);
}

void CAScoreViewPort::zoomToHeight(bool animate, bool force) {
	int maxY = (getMaxYExtended(_drawableCList)>getMaxYExtended(_drawableMList))?getMaxYExtended(_drawableCList):getMaxYExtended(_drawableMList);
	setWorldCoords(0,0,0,maxY,animate,force);
}

void CAScoreViewPort::zoomToFit(bool animate, bool force) {
	int maxX = ((_drawableCList.getMaxX() > _drawableMList.getMaxX())?_drawableCList.getMaxX():_drawableMList.getMaxX()); 
	int maxY = ((_drawableCList.getMaxY() > _drawableMList.getMaxY())?_drawableCList.getMaxY():_drawableMList.getMaxY()); 
	
	setWorldCoords(0, 0, maxX, maxY, animate, force);
}

/*!
	Sets the world coordinates of the viewport, so the given coordinates are the center of the new viewport area.
	If the area has for eg. negative top-left coordinates, the area is moved to the (0,0) coordinates if \a force is False.
	ViewPort's width and height stay intact.
	\warning Repaint is not done automatically!	
*/
void CAScoreViewPort::setCenterCoords(int x, int y, bool animate, bool force) {
	_checkScrollBarsDeadLock = true;
	setWorldX(x - (int)(0.5*_worldW), animate, force);
	setWorldY(y - (int)(0.5*_worldH), animate, force);
	_checkScrollBarsDeadLock = false;

	checkScrollBars();
}

/*!
	Zooms to the given level to given direction.
	\warning Repaint is not done automatically, if \a animate is False!
	
	\param z Zoom level. (1.0 = 100%, 1.5 = 150% etc.)
	\param x X coordinate of the point of the zoom direction. 
	\param y Y coordinate of the point of the zoom direction.
	\param animate Use smooth animated zoom.
	\param force Use the given world units despite their illegal values (like negative coordinates etc.).
*/
void CAScoreViewPort::setZoom(float z, int x, int y, bool animate, bool force) {
	bool zoomOut = false;
	if (_zoom - z > 0.0)
		zoomOut = true;

	if (animate) {
		if (!zoomOut) {
			_targetWorldX = ( _worldX - (_worldW/2) + x ) / 2;
			_targetWorldY = ( _worldY - (_worldH/2) + y ) / 2;
			_targetZoom = z;
			startAnimationTimer();
			return;
		} else {
			_targetWorldX = (int)(1.5*_worldX + 0.25*_worldW - 0.5*x);
			_targetWorldY = (int)(1.5*_worldY + 0.25*_worldH - 0.5*y);
			_targetZoom = z;
			startAnimationTimer();
			return;
		}
	}

	//set the world width - updates the zoom level zoom_ as well
	setWorldWidth((int)(drawableWidth() / z));
	setWorldHeight((int)(drawableHeight() / z));
	
	if (!zoomOut) { //zoom in
		//the new view's center coordinates will become the middle point of the current viewport center coords and the mouse pointer coords
		setCenterCoords( ( _worldX + (_worldW/2) + x ) / 2,
		                 ( _worldY + (_worldH/2) + y ) / 2,
		                 force );
	} else { //zoom out
		//the new view's center coordinates will become the middle point of the current viewport center coords and the mirrored over center pointer coords
		//worldX_ + (worldW_/2) + (worldX_ + (worldW_/2) - x)/2
		setCenterCoords( (int)(1.5*_worldX + 0.75*_worldW - 0.5*x),
		                 (int)(1.5*_worldY + 0.75*_worldH - 0.5*y),
		                 force );
	}
	
	checkScrollBars();
	calculateShadowNoteCoords();
}

/*!
	\fn void CAScoreViewPort::setZoom(float z, QPoint p, bool animate, bool force);
	Zooms to the given level to given direction.
	This is an overloaded member function, provided for convenience.
	\warning Repaint is not done automatically, if \a animate is False!
	
	\param z Zoom level. (1.0 = 100%, 1.5 = 150% etc.)
	\param p QPoint of the zoom direction.
	\param animate Use smooth animated zoom.
	\param force Use the given world units despite their illegal values (like negative coordinates etc.).
*/
		
/*!
	General Qt's paint event.
	All the music elements get actually rendered in this method.
*/
void CAScoreViewPort::paintEvent(QPaintEvent *e) {
	if (_holdRepaint)
		return;
	
	QPainter p(this);
	if (_drawBorder) {
		p.setPen(_borderPen);
		p.drawRect(0,0,width()-1,height()-1);
	}

	p.setClipping(true);
	if (_repaintArea) {
		p.setClipRect(QRect((int)((_repaintArea->x() - _worldX)*_zoom),
		                    (int)((_repaintArea->y() - _worldY)*_zoom),
		                    (int)(_repaintArea->width()*_zoom),
		                    (int)(_repaintArea->height()*_zoom)),
		              Qt::UniteClip);
	} else {
		p.setClipRect(QRect(_canvas->x(),
		                    _canvas->y(),
		                    _canvas->width(),
		                    _canvas->height()),
		              Qt::UniteClip);
	}
	
	
	//draw the background
	if (_repaintArea)
		p.fillRect((int)((_repaintArea->x() - _worldX)*_zoom), (int)((_repaintArea->y() - _worldY)*_zoom), (int)(_repaintArea->width()*_zoom), (int)(_repaintArea->height()*_zoom), _backgroundBrush);
	else
		p.fillRect(_canvas->x(), _canvas->y(), _canvas->width(), _canvas->height(), _backgroundBrush);

	QList<CADrawable *> l;
	//draw contexts
	int j = _drawableCList.size();
	if (_repaintArea)
		l = _drawableCList.findInRange(_repaintArea->x(), _repaintArea->y(), _repaintArea->width(),_repaintArea->height());
	else
		l = _drawableCList.findInRange(_worldX, _worldY, _worldW, _worldH);

	for (int i=0; i<l.size(); i++) {
		CADrawSettings s = {
	    	           _zoom,
	        	       (int)((l[i]->xPos() - _worldX) * _zoom),
		               (int)((l[i]->yPos() - _worldY) * _zoom),
	            	   drawableWidth(), drawableHeight(),
		               ((_currentContext == l[i])?Qt::blue:Qt::black)
		};
		l[i]->draw(&p, s);
	}

	//draw music elements
	if (_repaintArea)
		l = _drawableMList.findInRange(_repaintArea->x(), _repaintArea->y(), _repaintArea->width(),_repaintArea->height());
	else
		l = _drawableMList.findInRange(_worldX, _worldY, _worldW, _worldH);

	for (int i=0; i<l.size(); i++) {
		CADrawSettings s = {
		               _zoom,
		               (int)((l[i]->xPos() - _worldX) * _zoom),
		               (int)((l[i]->yPos() - _worldY) * _zoom),
		               drawableWidth(), drawableHeight(),
		               (_selection.contains((CADrawableMusElement*)l[i])?Qt::red:Qt::black)
		               };
		l[i]->draw(&p, s);
	}
	
	//draw shadow note
	if (_shadowNoteVisible) {
		for (int i=0; i<_shadowDrawableNote.size(); i++) {
			CADrawSettings s = {
			               _zoom,
			               (int)((_shadowDrawableNote[i]->xPos() - _worldX - _shadowDrawableNote[i]->width()/2) * _zoom + 0.5),
			               (int)((_shadowDrawableNote[i]->yPos() - _worldY) * _zoom + 0.5),
			               drawableWidth(), drawableHeight(),
			               (Qt::gray)
			               };
			_shadowDrawableNote[i]->draw(&p, s);
			if (_drawShadowNoteAccs) {
				CADrawableAccidental acc(_shadowNoteAccs, 0, 0, 0, _shadowDrawableNote[i]->yCenter());
				s.x -= (int)((acc.width()+2)*_zoom + 0.5);
				s.y = (int)((acc.yPos() - _worldY)*_zoom + 0.5);
				acc.draw(&p, s);
			}
		}
	}
	
	//flush the oldWorld coordinates as they're needed for the first repaint only
	_oldWorldX = _worldX; _oldWorldY = _worldY;
	_oldWorldW = _worldW; _oldWorldH = _worldH;
	
	if (_repaintArea) {
		delete _repaintArea;
		_repaintArea = 0;
		p.setClipping(false);
	}
}

/*!
	Draws the border with the given pen style, color, width and other pen settings.
	Enables border.
*/
void CAScoreViewPort::setBorder(const QPen pen) {
	_borderPen = pen;
	_drawBorder = true;
}

/*!
	Fills the background with the given brush style and color.
*/
void CAScoreViewPort::setBackground(const QBrush brush) {
	_backgroundBrush = brush;
}

/*!
	Disables the border.
*/
void CAScoreViewPort::unsetBorder() {
	_drawBorder = false;
}

/*!
	Called when the user resizes the widget.
	Note that repaint() event is also triggered when the internal drawable canvas changes its size (for eg. when scrollbars are shown/hidden) and the size of the viewport does not change.
*/
void CAScoreViewPort::resizeEvent(QResizeEvent *e) {
	setWorldCoords( _worldX, _worldY, qRound(drawableWidth() / _zoom), qRound(drawableHeight() / _zoom) );	
	// setWorld methods already check for scrollbars
}

/*!
	Checks whether the scrollbars are needed (the whole scene is not rendered) or not.
	Scrollbars get shown or hidden here.
	Repaint is done automatically, if needed.
*/
void CAScoreViewPort::checkScrollBars() {
	if ((isScrollBarVisible() != ScrollBarShowIfNeeded) || (_checkScrollBarsDeadLock))
		return;
	
	bool change = false;
	_holdRepaint = true;	// disable repaint until the scrollbar values are set
	_checkScrollBarsDeadLock = true;	// disable any further method calls until the method is over
	if ((((getMaxXExtended(_drawableMList) > getMaxXExtended(_drawableCList))?getMaxXExtended(_drawableMList):getMaxXExtended(_drawableCList)) - worldWidth() > 0) || (_hScrollBar->value()!=0)) { //if scrollbar is needed
		if (!_hScrollBar->isVisible()) {
			_hScrollBar->show();
			change = true;
		}
	} else // if the whole scene can be drawn on the canvas and the scrollbars are at position 0
		if (_hScrollBar->isVisible()) {
			_hScrollBar->hide();
			change = true;
		}
	
	if ((((getMaxYExtended(_drawableMList) > getMaxYExtended(_drawableCList))?getMaxYExtended(_drawableMList):getMaxYExtended(_drawableCList)) - worldHeight() > 0) || (_vScrollBar->value()!=0)) { //if scrollbar is needed
		if (!_vScrollBar->isVisible()) {
			_vScrollBar->show();
			change = true;
		}
	} else // if the whole scene can be drawn on the canvas and the scrollbars are at position 0
		if (_vScrollBar->isVisible()) {
			_vScrollBar->hide();
			change = true;
		}
	
	if (change) {
		setWorldHeight((int)(drawableHeight() / _zoom));
		setWorldWidth((int)(drawableWidth() / _zoom));
	}
	
	_holdRepaint = false;
	_checkScrollBarsDeadLock = false;
}

/*!
	Updates the shadow notes coordinates depending on the _xCursor and _yCursor coordiantes.
*/
void CAScoreViewPort::calculateShadowNoteCoords() {
	if (_currentContext?(_currentContext->drawableContextType() == CADrawableContext::DrawableStaff):0) {
		int pitch = ((CADrawableStaff*)_currentContext)->calculatePitch(_xCursor, _yCursor);	// the current staff has the real pitch we need
		for (int i=0; i<_shadowNote.size(); i++) {	// apply this pitch to all shadow notes in all staffs
			_shadowNote[i]->setPitch(pitch);
			_shadowDrawableNote[i]->setXPos(_xCursor);
			_shadowDrawableNote[i]->setYPos(
				static_cast<CADrawableStaff*>(_shadowDrawableNote[i]->drawableContext())->calculateCenterYCoord(pitch, _xCursor)
			);
		}
	}
}

/*!
	Processes the mousePressEvent().
	A new signal is emitted: CAMousePressEvent(), which usually gets processed by the parent class then.
*/
void CAScoreViewPort::mousePressEvent(QMouseEvent *e) {
	emit CAMousePressEvent(e, QPoint((int)(e->x() / _zoom) + _worldX, (int)(e->y() / _zoom) + _worldY), this);
}

/*!
	Processes the mouseMoveEvent().
	A new signal is emitted: CAMouseMoveEvent(), which usually gets processed by the parent class then.
*/
void CAScoreViewPort::mouseMoveEvent(QMouseEvent *e) {
	QPoint coords((int)(e->x() / _zoom) + _worldX, (int)(e->y() / _zoom) + _worldY);
	
	_xCursor = coords.x();
	_yCursor = coords.y();
	
	emit CAMouseMoveEvent(e, coords, this);

	if (_shadowNoteVisible) {
		calculateShadowNoteCoords();
		repaint();
	}
}

/*!
	Processes the wheelEvent().
	A new signal is emitted: CAWheelEvent(), which usually gets processed by the parent class then.
*/
void CAScoreViewPort::wheelEvent(QWheelEvent *e) {
	QPoint coords((int)(e->x() / _zoom) + _worldX, (int)(e->y() / _zoom) + _worldY);
	
	emit CAWheelEvent(e, coords, this);	

	_xCursor = (int)(e->x() / _zoom) + _worldX;	//TODO: _xCursor and _yCursor are still the old one. Somehow, _zoom level and _worldX/Y are not updated when emmiting CAWheel event. -Matevz 
	_yCursor = (int)(e->y() / _zoom) + _worldY;
}

/*!
	Processes the keyPressEvent().
	A new signal is emitted: CAKeyPressEvent(), which usually gets processed by the parent class then.
*/
void CAScoreViewPort::keyPressEvent(QKeyEvent *e) {
	emit CAKeyPressEvent(e, this);
}

void CAScoreViewPort::setScrollBarVisible(CAScrollBarVisibility status) {
	_scrollBarVisible = status;
	
	if ((status == ScrollBarAlwaysVisible) && (!_hScrollBar->isVisible())) {
		_hScrollBar->show();
		_vScrollBar->show();
		return;
	}
	
	if ((status == ScrollBarAlwaysHidden) && (_hScrollBar->isVisible())) {
		_hScrollBar->hide();
		_vScrollBar->hide();
		return;
	}
	
	checkScrollBars();
}

/*!
	Processes the Horizontal scroll bar event.
	This method is called when the horizontal scrollbar changes its value, let it be internally or due to user interaction.
*/
void CAScoreViewPort::HScrollBarEvent(int val) {
	if ((_allowManualScroll) && (!_hScrollBarDeadLock)) {
		setWorldX(val);
		repaint();
	}
}

/*!
	Processes the Vertical scroll bar event.
	This method is called when the horizontal scrollbar changes its value, let it be internally or due to user interaction.
*/
void CAScoreViewPort::VScrollBarEvent(int val) {
	if ((_allowManualScroll) && (!_vScrollBarDeadLock)) {
		setWorldY(val);
		repaint();
	}
}

void CAScoreViewPort::leaveEvent(QEvent *e) {
	_shadowNoteVisibleOnLeave = _shadowNoteVisible;
	_shadowNoteVisible = false;
	repaint();
}
		
void CAScoreViewPort::enterEvent(QEvent *e) {
	_shadowNoteVisible = _shadowNoteVisibleOnLeave;
	repaint();
}

void CAScoreViewPort::startAnimationTimer() {
	_animationTimer->stop();
	_animationStep = 0;
	_animationTimer->start();
	on__animationTimer_timeout();
}

/*!
	Selects the next music element in the current context.
	Returns a pointer to the newly selected drawable music element or 0, if such an element doesn't exist or the selection is empty.
	
	This method is usually called when using the right arrow key.
*/
CADrawableMusElement *CAScoreViewPort::selectNextMusElement() {
	if (_selection.isEmpty())
		return 0;
	
	CAMusElement *musElement = _selection.back()->musElement();
	musElement = musElement->context()->findNextMusElement(musElement);
	if (!musElement)
		return 0;
	
	return selectMElement(musElement);
}

/*!
	Selects the previous music element in the current context.
	Returns a pointer to the newly selected drawable music element or 0, if such an element doesn't exist or the selection is empty.
	
	This method is usually called when using the left arrow key.
*/
CADrawableMusElement *CAScoreViewPort::selectPrevMusElement() {
	if (_selection.isEmpty())
		return 0;
	
	CAMusElement *musElement = _selection.front()->musElement();
	musElement = musElement->context()->findPrevMusElement(musElement);
	if (!musElement)
		return 0;
	
	return selectMElement(musElement);
}

/*!
	Selects the upper music element in the current context.
	Returns a pointer to the newly selected drawable music element or 0, if such an element doesn't exist or the selection is empty.
	
	This method is usually called when using the up arrow key.
	\todo Still needs to be written. Currently, it only returns the currently selected element.
*/
CADrawableMusElement *CAScoreViewPort::selectUpMusElement() {
	if (_selection.isEmpty())
		return 0;
	
	return _selection.front();
}

/*!
	Selects the lower music element in the current context.
	Returns a pointer to the newly selected drawable music element or 0, if such an element doesn't exist or the selection is empty.
	
	This method is usually called when using the up arrow key.
	\todo Still needs to be written. Currently, it only returns the currently selected element.
*/
CADrawableMusElement *CAScoreViewPort::selectDownMusElement() {
	if (_selection.isEmpty())
		return 0;
	
	return _selection.front();
}

/*!
	Adds the drawable music element of the given abstract music element \a elt to the selection.
	Returns a pointer to its drawable element or 0, if the music element is not part of this score viewport.
*/
CADrawableMusElement *CAScoreViewPort::addToSelection(CAMusElement *elt) {
	for (int i=0; i<_drawableMList.size(); i++) {
		if (((CADrawableMusElement*)_drawableMList.at(i))->musElement() == elt)
			_selection << (CADrawableMusElement*)_drawableMList.at(i);
	}
	
	return _selection.back();
}

/*!
	Adds the given list of abstract music elements to the selection.
*/
void CAScoreViewPort::addToSelection(const QList<CAMusElement*> elts) {
	for (int i=0; i<_drawableMList.size(); i++) {
		for (int j=0; j<elts.size(); j++) {
			if (elts[j] == ((CADrawableMusElement*)_drawableMList.at(i))->musElement())
				_selection << (CADrawableMusElement*)_drawableMList.at(i);
		}
	}
}

/*!
	Finds the drawable instance of the given abstract music element.
	
	\sa findCElement()
*/
CADrawableMusElement *CAScoreViewPort::findMElement(CAMusElement *elt) {
	for (int i=0; i<_drawableMList.size(); i++)
		if (static_cast<CADrawableMusElement*>(_drawableMList.at(i))->musElement()==elt)
			return static_cast<CADrawableMusElement*>(_drawableMList.at(i));
}	
	
/*!
	Finds the drawable instance of the given abstract context.
	
	\sa findMElement()
*/
CADrawableContext *CAScoreViewPort::findCElement(CAContext *context) {
	for (int i=0; i<_drawableCList.size(); i++)
		if (static_cast<CADrawableContext*>(_drawableCList.at(i))->context()==context)
			return static_cast<CADrawableContext*>(_drawableCList.at(i));
}
	
/*!
	Returns the maximum X of the viewable World a little bigger to make insertion at the end easy.
*/
int CAScoreViewPort::getMaxXExtended(CAKDTree &v) {
	return v.getMaxX() + RIGHT_EXTRA_SPACE;
}

/*!
	Returns the maximum Y of the viewable World a little bigger to make insertion at the end easy.
*/
int CAScoreViewPort::getMaxYExtended(CAKDTree &v) {
	return v.getMaxY() + BOTTOM_EXTRA_SPACE;
}

/*!
	\fn CASheet *CAScoreViewPort::sheet()
	Returns the pointer to the viewport's sheet it represents.
*/

/*!
	\fn void CAScoreViewPort::addToSelection(CADrawableMusElement *elt)
	Adds the given drawable music element \a elt to the current selection.
*/

/*!
	\fn void CAScoreViewPort::addToSelection(const QList<CADrawableMusElement*> list)
	Adds the given list of drawable music elements \a list to the current selection.
*/

/*!
	\fn bool CAScoreViewPort::removeFromSelection(CADrawableMusElement *elt)
	Removes the given drawable music element \a elt from the selection, if it exists.
	Returns True, if element existed in the selection and was removed, false otherwise.
*/

/*!
	\fn void CAScoreViewPort::clearSelection()
	Clears the current selection. Its behaviour is the same as calling clearMSelection() and clearCSelection().
*/

/*!
	\fn QList<CAMusElement*> CAScoreViewPort::selection()
	Returns a list of the currently selected drawable music elements.
*/

/*!
	\var bool CAScoreViewPort::_allowManualScroll
	This property holds whether a user interaction with the scrollbars actually triggers the scroll of the viewport.
*/

/*!
	\enum CAScoreViewPort::CAScrollBarVisibility
	Different behaviour of the scroll bars:
		- ScrollBarAlwaysVisible - scrollbars are always visible, no matter if the whole scene can be rendered on canvas or not
		- ScrollBarAlwaysHidden - scrollbars are always hidden, no matter if the whole scene can be rendered on canvas or not
		- ScrollBarShowIfNeeded - scrollbars are visible, if they are needed (the current viewport area is too small to render the whole
		  scene), otherwise hidden. This is default behaviour.
*/

/*!
	\fn float CAScoreViewPort::zoom()
	Returns the zoom level of the viewport (1.0 = 100%, 1.5 = 150% etc.).
*/

/*!
	\fn void CAScoreViewPort::setRepaintArea(QRect *area)
	Sets the area to be repainted, not the whole widget.
	
	\sa clearRepaintArea()
*/ 

/*!
	\fn void CAScoreViewPort::clearRepaintArea()
	Disables and deletes the area to be repainted.
	
	\sa setRepaintArea()
*/
