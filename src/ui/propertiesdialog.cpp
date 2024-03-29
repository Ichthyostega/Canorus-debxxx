/*!
	Copyright (c) 2007, Matevž Jekovec, Canorus development team
	All Rights Reserved. See AUTHORS for a complete list of authors.

	Licensed under the GNU GENERAL PUBLIC LICENSE. See COPYING for details.
*/

#include <QTreeWidgetItem>
#include <QDateTime>
#include <QHeaderView> // needed to hide header widget from document tree
#include <QDate>

#include "ui/propertiesdialog.h"

#include "canorus.h"
#include "core/undo.h"

#include "score/document.h"
#include "score/sheet.h"
#include "score/staff.h"
#include "score/voice.h"
#include "score/lyricscontext.h"
#include "score/functionmarkcontext.h"

/*!
	\class CAPropertiesDialog
	\brief Advanced Document, Sheet, Staff etc. properties

	This dialog offers changing all the settings for Canorus objects. It is similar to CASettingsDialog.
	On the left it shows you a tree widget view of the current document and allows you to select
	one of the objects and sub-objects. On the right, the object properties are shown. Changes are
	managed using Ok, Apply and Cancel buttons.

	To use this dialog, call one of the static methods documentProperties(), sheetProperties(), contextProperties()
	or voiceProperties() and pass the current document, sheet, context or voice. Methods will generate and show
	the dialog and delete it in the end.

	Actual objects properties widgets (properties widget for document, sheet etc.) are stored inside *.ui files.
*/

CAPropertiesDialog::CAPropertiesDialog( CADocument *doc, QWidget *parent )
 : QDialog(parent) {
	_document = doc;

	Ui::uiPropertiesDialog::setupUi( this );

	uiDocumentTree->header()->hide();
	buildTree();
}

CAPropertiesDialog::~CAPropertiesDialog() {
}

/*!
	Fills the content of the document tree on the left.
	Tree always shows the whole Document structure.
*/
void CAPropertiesDialog::buildTree() {
	QWidget *w=0;

	// get current item
	QTreeWidgetItem *curItem=uiDocumentTree->currentItem();
	CADocument *curDocument=0; CASheet *curSheet=0; CAContext *curContext=0; CAVoice *curVoice=0;
	if ( curItem ) {
		if ( _documentItem==curItem )
			curDocument = _document;
		else
		if ( _sheetItem.contains(curItem) )
			curSheet = _sheetItem[curItem];
		else
		if ( _contextItem.contains(curItem) )
			curContext = _contextItem[curItem];
		else
		if ( _voiceItem.contains(curItem) )
			curVoice= _voiceItem[curItem];
	}

	QTreeWidgetItem *docItem=0;
	uiDocumentTree->clear();
	_documentItem=0; _sheetItem.clear(); _contextItem.clear(); _voiceItem.clear();
	if (_document) {
		w = new CADocumentProperties( _document, this );
		_documentPropertiesWidget = w;
		uiPropertiesWidget->addWidget( w );
		updateDocumentProperties( _document );

		docItem = new QTreeWidgetItem( uiDocumentTree );
		docItem->setText( 0, tr("Document") );
		docItem->setIcon( 0, QIcon("images:document/document.svg") );
		_documentItem = docItem;

		for ( int i=0; i<_document->sheetList().size(); i++) {
			w = new CASheetProperties( this );
			_sheetPropertiesWidget[ _document->sheetList()[i] ] = w;
			uiPropertiesWidget->addWidget( w );
			updateSheetProperties( _document->sheetList()[i] );

			QTreeWidgetItem *sheetItem=0;
			sheetItem = new QTreeWidgetItem( docItem );
			sheetItem->setText( 0, _document->sheetList()[i]->name() );
			sheetItem->setIcon( 0, QIcon("images:document/sheet.svg") );
			_sheetItem[ sheetItem ] = _document->sheetList()[i];

			for ( int j=0; j<_document->sheetList()[i]->contextList().size(); j++ ) {
				QTreeWidgetItem *contextItem=0;
				contextItem = new QTreeWidgetItem( sheetItem );
				contextItem->setText( 0, _document->sheetList()[i]->contextList()[j]->name() );
				_contextItem[ contextItem ] = _document->sheetList()[i]->contextList()[j];

				if (dynamic_cast<CAStaff*>( _document->sheetList()[i]->contextList()[j] )) {
					contextItem->setIcon( 0, QIcon("images:document/staff.svg") );
					w = new CAStaffProperties( this );
					uiPropertiesWidget->addWidget( w );
					_contextPropertiesWidget[ _document->sheetList()[i]->contextList()[j] ] = w;
					CAStaff *s = static_cast<CAStaff*>(_document->sheetList()[i]->contextList()[j]);
					updateStaffProperties( s );

					for (int k=0; k<s->voiceList().size(); k++) {
						QWidget *v = new CAVoiceProperties( this );
						uiPropertiesWidget->addWidget( v );
						_voicePropertiesWidget[ s->voiceList()[k] ] = v;
						updateVoiceProperties( s->voiceList()[k] );

						QTreeWidgetItem *voiceItem=0;
						voiceItem = new QTreeWidgetItem( contextItem );
						voiceItem->setText( 0, s->voiceList()[k]->name() );
						voiceItem->setIcon( 0, QIcon("images:document/voice.svg") );
						_voiceItem[ voiceItem ] = s->voiceList()[k];
					}
				} else
				if (dynamic_cast<CALyricsContext*>( _document->sheetList()[i]->contextList()[j] )) {
					contextItem->setIcon( 0, QIcon("images:document/lyricscontext.svg") );
					w = new CALyricsContextProperties( this );
					uiPropertiesWidget->addWidget( w );
					_contextPropertiesWidget[ _document->sheetList()[i]->contextList()[j] ] = w;
					updateLyricsContextProperties( static_cast<CALyricsContext*>(_document->sheetList()[i]->contextList()[j]) );
				} else
				if (dynamic_cast<CAFunctionMarkContext*>( _document->sheetList()[i]->contextList()[j] )) {
				contextItem->setIcon( 0, QIcon("images:document/fmcontext.svg") );
				w = new CAFunctionMarkContextProperties( this );
					uiPropertiesWidget->addWidget( w );
					_contextPropertiesWidget[ _document->sheetList()[i]->contextList()[j] ] = w;
					updateFunctionMarkContextProperties( static_cast<CAFunctionMarkContext*>(_document->sheetList()[i]->contextList()[j]) );
				}

			}
		}

		uiDocumentTree->addTopLevelItem( docItem );
		uiDocumentTree->expandAll();
	}

	// updates to the current item
	if ( curItem ) {
		if (curDocument)
			uiDocumentTree->setCurrentItem( _documentItem );
		else
		if (curSheet)
			uiDocumentTree->setCurrentItem( _sheetItem.key(curSheet) );
		else
		if (curContext)
			uiDocumentTree->setCurrentItem( _contextItem.key(curContext) );
		else
		if (curVoice)
			uiDocumentTree->setCurrentItem( _voiceItem.key(curVoice) );
	}
}

void CAPropertiesDialog::documentProperties( CADocument *doc, QWidget *parent ) {
	if (doc) {
		CAPropertiesDialog pd( doc, parent );
		pd.uiDocumentTree->setCurrentItem( pd.documentItem() );
		pd.exec();
	}
}

void CAPropertiesDialog::sheetProperties( CASheet *sheet, QWidget *parent ) {
	if ( sheet && sheet->document() ) {
		CAPropertiesDialog pd( sheet->document(), parent );
		pd.uiDocumentTree->setCurrentItem( pd.sheetItem().key(sheet) );
		pd.exec();
	}
}

void CAPropertiesDialog::contextProperties( CAContext *context, QWidget *parent ) {
	if ( context && context->sheet() && context->sheet()->document() ) {
		CAPropertiesDialog pd( context->sheet()->document(), parent );
		pd.uiDocumentTree->setCurrentItem( pd.contextItem().key(context) );
		pd.exec();
	}
}

void CAPropertiesDialog::voiceProperties( CAVoice *voice, QWidget *parent ) {
	if ( voice && voice->staff() && voice->staff()->sheet() &&
	     voice->staff()->sheet()->document() ) {
		CAPropertiesDialog pd( voice->staff()->sheet()->document(), parent );
		pd.uiDocumentTree->setCurrentItem( pd.voiceItem().key(voice) );
		pd.exec();
	}
}

void CADocumentProperties::on_uiComposer_editingFinished() {
	QString curText = uiCopyright->currentText();
	uiCopyright->clear();

	QString startMsg = "(C)";
	int yearStart = _document->dateCreated().date().year();
	int yearCur = QDate::currentDate().year();

	if (yearStart && yearStart!=yearCur)
		startMsg += QString::number(yearStart) + "-";
	startMsg += QString::number(yearCur);
	if (uiComposer->text().size())
		startMsg += " " + uiComposer->text() + ",";

	uiCopyright->addItem( startMsg + " " + tr("CC, Some rights reserved", "copyright") );
	uiCopyright->addItem( startMsg + " " + tr("Public domain", "copyright") );
	uiCopyright->addItem( startMsg + " " + tr("All rights reserved", "copyright") );

	uiCopyright->setEditText( curText );
}

void CAPropertiesDialog::on_uiDocumentTree_currentItemChanged( QTreeWidgetItem *cur, QTreeWidgetItem *prev ) {
	if (!cur)
		return;

	uiElementName->setText( cur->text(0) );

	if ( _documentItem == cur ) {
		updateDocumentProperties( _document );

		// update uiUp/uiDown buttons
		uiUp->setEnabled( false );
		uiDown->setEnabled( false );
	} else
	if ( _sheetItem.contains(cur) ) {
		updateSheetProperties( _sheetItem[cur] );

		// update uiUp/uiDown buttons
		uiUp->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)>0 )
			uiUp->setEnabled( true );

		uiDown->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)<cur->parent()->childCount()-1 )
			uiDown->setEnabled( true );
	} else
	if ( _contextItem.contains(cur) ) {
		switch ( _contextItem[cur]->contextType() ) {
			case CAContext::Staff:
				updateStaffProperties( static_cast<CAStaff*>( _contextItem[cur] ) );
				break;
			case CAContext::LyricsContext:
				updateLyricsContextProperties( static_cast<CALyricsContext*>( _contextItem[cur] ) );
				break;
			case CAContext::FunctionMarkContext:
				updateFunctionMarkContextProperties( static_cast<CAFunctionMarkContext*>( _contextItem[cur] ) );
				break;
			case CAContext::FiguredBassContext:
				fprintf(stderr,"Warning: CAPropertiesDialog::on_uiDocumentTree_currentItemChanged - Unhandled Type %d",_contextItem[cur]->contextType());
				break;
		}

		// update uiUp/uiDown buttons
		uiUp->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)>0 ) // context has another context above
			uiUp->setEnabled( true );

		uiDown->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)<cur->parent()->childCount()-1 ) // context has another context below
			uiDown->setEnabled( true );
	} else
	if ( _voiceItem.contains(cur) ) {
		updateVoiceProperties( _voiceItem[cur] );

		// update uiUp/uiDown buttons
		uiUp->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)>0 ) // voice has another voice above
			uiUp->setEnabled( true );

		uiDown->setEnabled( false );
		if ( cur->parent()->childCount()>1 && cur->parent()->indexOfChild(cur)<cur->parent()->childCount()-1 ) // voice has another voice below
			uiDown->setEnabled( true );
	}
}

void CAPropertiesDialog::on_uiButtonBox_clicked( QAbstractButton* button ) {
	if ( uiButtonBox->standardButton(button) == QDialogButtonBox::Ok ) {
		applyProperties();
		hide();
	} else
	if ( uiButtonBox->standardButton(button) == QDialogButtonBox::Cancel ) {
		hide();
	} else
	if ( uiButtonBox->standardButton(button) == QDialogButtonBox::Apply ) {
		applyProperties();
	}
}

/*!
	Called when "Apply" button is clicked.
*/
void CAPropertiesDialog::applyProperties() {
	//QTreeWidgetItem *item = uiDocumentTree->topLevelItem(0);

	CACanorus::undo()->createUndoCommand( _document, tr("apply properties", "undo") );
	CACanorus::undo()->pushUndoCommand();

	createDocumentFromTree();

	// store Document properties
	CADocumentProperties *dp = static_cast<CADocumentProperties*>(_documentPropertiesWidget);
	_document->setTitle( dp->uiTitle->text() );
	_document->setSubtitle( dp->uiSubtitle->text() );
	_document->setComposer( dp->uiComposer->text() );
	_document->setArranger( dp->uiArranger->text() );
	_document->setPoet( dp->uiPoet->text() );
	_document->setTextTranslator( dp->uiTextTranslator->text() );
	_document->setDedication( dp->uiDedication->text() );
	_document->setCopyright( dp->uiCopyright->currentText() );
	_document->setComments( dp->uiComments->toPlainText() );

	// store Sheet properties
	/*for (int i=0; i<_sheetPropertiesWidget.keys().size(); i++) {
		CASheet *s = _sheetPropertiesWidget.keys().at(i);
	}*/

	// store Context properties
	for (int i=0; i<_contextPropertiesWidget.keys().size(); i++) {
		CAContext *c = _contextPropertiesWidget.keys().at(i);
		switch (c->contextType()) {
			case CAContext::Staff: {
				CAStaff *staff = static_cast<CAStaff*>(c);
				CAStaffProperties *sp = static_cast<CAStaffProperties*>(_contextPropertiesWidget[staff]);
				staff->setNumberOfLines( sp->uiNumberOfLines->value() );
				break;
			}
			case CAContext::LyricsContext: {
				break;
			}
			case CAContext::FunctionMarkContext: {
				break;
			}
			case CAContext::FiguredBassContext:
				fprintf(stderr,"Warning: CAPropertiesDialog::applyProperties - Unhandled Type %d",c->contextType());
				break;
		}
	}

	// store Voice properties
	for (int i=0; i<_voicePropertiesWidget.keys().size(); i++) {
		CAVoice *voice = _voicePropertiesWidget.keys().at(i);
		CAVoiceProperties *vp = static_cast<CAVoiceProperties*>(_voicePropertiesWidget[ voice ]);
		voice->setMidiChannel( vp->uiMidiChannel->value()-1 );
		voice->setMidiPitchOffset( vp->uiMidiPitchOffset->value() );
	}

	CACanorus::rebuildUI( _document );
	buildTree();
}

/*!
	Repositiones document structure as placed in the document tree widget.
	This is used when a user changes the order of document elements and the elements need to be
	readded to its parent structures.
	This method is usually called when applying the changes.
*/
void CAPropertiesDialog::createDocumentFromTree() {
	while ( _document->sheetList().size() )
		_document->removeSheet( _document->sheetList()[0] );

	QTreeWidgetItem *cur = uiDocumentTree->topLevelItem(0);

	for ( int i=0; i<cur->childCount(); i++ ) {
		CASheet *sheet = _sheetItem[cur->child(i)];
		while ( sheet->contextList().size() )
			sheet->removeContext( sheet->contextList()[0] );

		_document->addSheet(sheet);

		for ( int j=0; j<cur->child(i)->childCount(); j++ ) {
			CAContext *c = _contextItem[cur->child(i)->child(j)];
			switch ( c->contextType() ) {
				case CAContext::Staff: {
					CAStaff *s = static_cast<CAStaff*>(c);
					while ( s->voiceList().size() )
						s->removeVoice( s->voiceList()[0] );

					for ( int k=0; k<cur->child(i)->child(j)->childCount(); k++ ) {
						s->addVoice( _voiceItem[cur->child(i)->child(j)->child(k)] );
					}
				}
				default:
					sheet->addContext(c);
					break;
			}
		}
	}
}

void CAPropertiesDialog::on_uiUp_clicked( bool down ) {
	QTreeWidgetItem *cur = uiDocumentTree->currentItem();
	QTreeWidgetItem *parent = cur->parent();
	int idx;

	if ( !parent || (idx = parent->indexOfChild(cur))==-1 )
		return;

	parent->removeChild(cur);
	parent->insertChild( idx-1, cur );
	uiDocumentTree->setCurrentItem( cur );
	uiDocumentTree->expandAll();
}

void CAPropertiesDialog::on_uiDown_clicked( bool down ) {
	QTreeWidgetItem *cur = uiDocumentTree->currentItem();
	QTreeWidgetItem *parent = cur->parent();
	int idx;

	if ( !parent || (idx = parent->indexOfChild(cur))==-1 )
		return;

	parent->removeChild(cur);
	parent->insertChild( idx+1, cur );
	uiDocumentTree->setCurrentItem( cur );
	uiDocumentTree->expandAll();
}

void CAPropertiesDialog::updateDocumentProperties( CADocument *doc ) {
	CADocumentProperties *dp = static_cast<CADocumentProperties*>(_documentPropertiesWidget);

	dp->uiTitle->setText( doc->title() );
	dp->uiSubtitle->setText( doc->subtitle() );
	dp->uiComposer->setText( doc->composer() );
	dp->uiArranger->setText( doc->arranger() );
	dp->uiPoet->setText( doc->poet() );
	dp->uiTextTranslator->setText( doc->textTranslator() );
	dp->uiDedication->setText( doc->dedication() );
	dp->on_uiComposer_editingFinished();
	dp->uiCopyright->setEditText( doc->copyright() );
	dp->uiComments->setText( doc->comments() );

	uiPropertiesWidget->setCurrentWidget( dp );
}

void CAPropertiesDialog::updateSheetProperties( CASheet *sheet ) {
	uiPropertiesWidget->setCurrentWidget( _sheetPropertiesWidget[sheet] );
}

void CAPropertiesDialog::updateStaffProperties( CAStaff *staff ) {
	CAStaffProperties *sp = static_cast<CAStaffProperties*>( _contextPropertiesWidget[staff] );
	sp->uiNumberOfLines->setValue( staff->numberOfLines() );

	uiPropertiesWidget->setCurrentWidget( _contextPropertiesWidget[staff] );
}

void CAPropertiesDialog::updateVoiceProperties( CAVoice *voice ) {
	static_cast<CAVoiceProperties*>(_voicePropertiesWidget[voice])->uiMidiChannel->setValue( voice->midiChannel()+1 );
	static_cast<CAVoiceProperties*>(_voicePropertiesWidget[voice])->uiMidiPitchOffset->setValue( voice->midiPitchOffset() );

	uiPropertiesWidget->setCurrentWidget( _voicePropertiesWidget[voice] );
}

void CAPropertiesDialog::updateLyricsContextProperties( CALyricsContext *lc ) {
	uiPropertiesWidget->setCurrentWidget( _contextPropertiesWidget[lc] );
}

void CAPropertiesDialog::updateFunctionMarkContextProperties( CAFunctionMarkContext *fmc ) {
	uiPropertiesWidget->setCurrentWidget( _contextPropertiesWidget[fmc] );
}
