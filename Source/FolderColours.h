#pragma once
class FolderColourDots final:public juce::PopupMenu::CustomComponent {
 struct Dot:juce::Button {
  juce::Colour colour;Dot():Button(juce::String()){setClickingTogglesState(true);}
  void paintButton(juce::Graphics& g,bool over,bool)override{auto circle=getLocalBounds().toFloat().withSizeKeepingCentre(13,13);g.setColour(colour);g.fillEllipse(circle);if(getToggleState()){g.setColour(juce::Colours::white);g.drawEllipse(circle.expanded(3),1.4f);}else if(over){g.setColour(juce::Colours::white.withAlpha(.35f));g.drawEllipse(circle.expanded(3),1.f);}}
 };
 std::array<Dot,8> dots;int mask=0;std::function<bool(int)> changed;
public:
 static juce::Colour colour(int index){static constexpr juce::uint32 colours[]{0xffee625b,0xffed9c45,0xffe4cd58,0xff72bd69,0xff56bdc7,0xff639ce3,0xffb17bd5,0xffa6a6ab};return juce::Colour(colours[juce::jlimit(0,7,index)]);}
 FolderColourDots(int initial,std::function<bool(int)> callback):CustomComponent(false),mask(initial&255),changed(std::move(callback)){
  static const char* names[]{"Red","Orange","Yellow","Green","Cyan","Blue","Purple","Grey"};
  for(int i=0;i<8;++i){auto& dot=dots[i];dot.colour=colour(i);dot.setName(names[i]);dot.setTitle(names[i]);dot.setToggleState((mask&(1<<i))!=0,juce::dontSendNotification);dot.onClick=[this,i]{int proposed=mask^(1<<i);if(!changed||changed(proposed))mask=proposed;dots[i].setToggleState((mask&(1<<i))!=0,juce::dontSendNotification);};addAndMakeVisible(dot);}
 }
 void getIdealSize(int& width,int& height)override{width=232;height=36;}
 void resized()override{int step=(getWidth()-16)/8;for(int i=0;i<8;++i)dots[i].setBounds(8+i*step,3,step,getHeight()-6);}
};
