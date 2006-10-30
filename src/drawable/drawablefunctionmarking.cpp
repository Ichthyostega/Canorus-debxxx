/** @file drawable/drawablefunctionmarking.cpp
 * 
 * Copyright (c) 2006, Matevž Jekovec, Canorus development team
 * All Rights Reserved. See AUTHORS for a complete list of authors.
 * 
 * Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
 */

#include <QPainter>
#include <QFont>

#include "drawable/drawablefunctionmarking.h"
#include "drawable/drawablefunctionmarkingcontext.h"
#include "core/functionmarking.h"

CADrawableFunctionMarking::CADrawableFunctionMarking(CAFunctionMarking *function, CADrawableFunctionMarkingContext *context, int x, int y)
 : CADrawableMusElement(function, context, x, y) {
 	_drawableMusElementType = CADrawableMusElement::DrawableFunctionMarking;
 	
 	_extenderLine = false;
}

CADrawableFunctionMarking::~CADrawableFunctionMarking() {
	
}

void CADrawableFunctionMarking::draw(QPainter *p, const CADrawSettings s) {
	p->setFont(QFont("FreeSans"));
	
	QString text;
	switch (functionMarking()->function()) {
		case CAFunctionMarking::I: text="I"; break;
		case CAFunctionMarking::II: text="II"; break;
		case CAFunctionMarking::III: text="III"; break;
		case CAFunctionMarking::IV: text="IV"; break;
		case CAFunctionMarking::V: text="V"; break;
		case CAFunctionMarking::VI: text="VI"; break;
		case CAFunctionMarking::VII: text="VII"; break;
		case CAFunctionMarking::T: text="T"; break;
		case CAFunctionMarking::S: text="S"; break;
		case CAFunctionMarking::D: text="D"; break;
		case CAFunctionMarking::F: text="F"; break;
		case CAFunctionMarking::N: text="N"; break;
		case CAFunctionMarking::L: text="L"; break;
	}
	p->drawText(s.x, s.y, text);
}

CADrawableFunctionMarking *CADrawableFunctionMarking::clone() {
	return new CADrawableFunctionMarking(functionMarking(), drawableFunctionMarkingContext(), xPos(), yPos());
}
