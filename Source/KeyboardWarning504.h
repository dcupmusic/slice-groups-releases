#pragma once
// Explicit focus traversal also works when macOS Full Keyboard Access is off.
class KeyboardWarning504 final:public juce::AlertWindow,private juce::KeyListener {
 int selected=0;
 bool keyPressed(const juce::KeyPress& key,juce::Component*)override{return keyPressed(key);}
public:
 KeyboardWarning504(juce::String title,juce::String message,juce::String confirm,juce::Component* parent):juce::AlertWindow(title,message,juce::MessageBoxIconType::WarningIcon,parent){addButton("Cancel",0);addButton(confirm,1);setWantsKeyboardFocus(true);for(int i=0;i<2;++i){auto* b=getButton(i);b->setWantsKeyboardFocus(true);b->setExplicitFocusOrder(i+1);b->setMouseClickGrabsKeyboardFocus(true);b->addKeyListener(this);}}
 ~KeyboardWarning504()override{for(int i=0;i<getNumButtons();++i)getButton(i)->removeKeyListener(this);}
 void open(juce::ModalComponentManager::Callback* callback){enterModalState(true,callback,true);getButton(0)->grabKeyboardFocus();}
 void paintOverChildren(juce::Graphics& g)override{auto* b=getButton(focusedOption());if(b){g.setColour(juce::Colour(0xffbd665d));g.drawRect(b->getBounds().reduced(1),2);}}
 int focusedOption()const{for(int i=0;i<getNumButtons();++i)if(getButton(i)->hasKeyboardFocus(true))return i;return selected;}
 bool keyPressed(const juce::KeyPress& key)override{if(key.isKeyCode(juce::KeyPress::tabKey)){selected=(focusedOption()+(key.getModifiers().isShiftDown()?-1:1)+getNumButtons())%getNumButtons();getButton(selected)->grabKeyboardFocus();repaint();return true;}if(key.isKeyCode(juce::KeyPress::returnKey)){getButton(focusedOption())->triggerClick();return true;}if(key.isKeyCode(juce::KeyPress::escapeKey)){getButton(0)->triggerClick();return true;}return juce::AlertWindow::keyPressed(key);}
};
