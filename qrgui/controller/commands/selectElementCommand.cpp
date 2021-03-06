#include "selectElementCommand.h"

using namespace qReal::commands;

SelectElementCommand::SelectElementCommand(EditorViewScene const *scene
		, Id const &id, bool shouldSelect, bool forceValueChange)
	: ElementCommand(scene, id)
	, mNewState(shouldSelect)
	, mForceValueChange(forceValueChange)
{
}

SelectElementCommand::SelectElementCommand(EditorView const *view
		, Id const &id, bool shouldSelect, bool forceValueChange)
	: ElementCommand(view->editorViewScene(), id)
	, mNewState(shouldSelect)
	, mForceValueChange(forceValueChange)
{
}

bool SelectElementCommand::execute()
{
	ElementCommand::execute();
	mOldState = isSelected();
	return setSelectionState(mNewState);
}

bool SelectElementCommand::restoreState()
{
	ElementCommand::restoreState();
	return setSelectionState(mOldState);
}

bool SelectElementCommand::setSelectionState(bool select)
{
	if (!mElement) {
		return false;
	}
	if (mForceValueChange) {
		mElement->setSelected(!isSelected());
	}
	mElement->setSelected(select);
	return true;
}

bool SelectElementCommand::isSelected() const
{
	return mElement && mElement->isSelected();
}
