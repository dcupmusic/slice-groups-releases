#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
struct NumericEscapeCommit:juce::KeyListener{bool keyPressed(const juce::KeyPress& key,juce::Component* origin)override{if(key.getKeyCode()!=juce::KeyPress::escapeKey&&key.getKeyCode()!=juce::KeyPress::returnKey)return false;if(auto* label=origin->findParentComponentOfClass<juce::Label>()){auto* input=dynamic_cast<juce::TextEditor*>(origin);bool empty=input&&!input->getText().containsAnyOf("0123456789");if(!empty&&key.getKeyCode()==juce::KeyPress::returnKey)return false;label->hideEditor(empty);label->giveAwayKeyboardFocus();return true;}return false;}static NumericEscapeCommit& instance(){static NumericEscapeCommit listener;return listener;}};
struct NumericInputFilter:juce::TextEditor::InputFilter {
 static bool valid(juce::String text,bool complete=false){text=text.trim();bool digit=false,decimal=false;int i=0;for(auto c:text){if((c=='-'||c=='+')&&i==0){}else if(c=='.'&&!decimal)decimal=true;else if(c>='0'&&c<='9')digit=true;else return false;++i;}return !complete||digit;}
 juce::String filterNewText(juce::TextEditor& editor,const juce::String& input)override{if(input.isEmpty())return input;auto range=editor.getHighlightedRegion();auto candidate=editor.getText().replaceSection(range.getStart(),range.getLength(),input);return candidate.length()<=32&&valid(candidate)?input:editor.getHighlightedText();}
 static juce::Label* sliderLabel(juce::Label* label){auto before=label->onEditorShow;label->onEditorShow=[label,before]{if(before)before();if(auto* input=label->getCurrentTextEditor()){input->setText(input->getText().retainCharacters("0123456789.-+"),false);input->setInputFilter(new NumericInputFilter(),true);input->addKeyListener(&NumericEscapeCommit::instance());input->selectAll();}};return label;}
};
namespace palette {
inline juce::Colour bg() { return juce::Colour(0xff242424); }
inline juce::Colour panel() { return juce::Colour(0xff2c2c2c); }
inline juce::Colour muted() { return juce::Colour(0xffa3a3a3); }
inline juce::Colour accent() { return juce::Colour(0xffdf4c42); }
inline juce::Colour note(int note) { return note < 0 ? juce::Colour(0xff666666) : juce::Colour::fromHSV(std::fmod((note-36)*.097f+1.f,1.f),.33f,.76f,1.f); }
inline juce::String noteName(int note) { return juce::MidiMessage::getMidiNoteName(note,true,true,3); }
inline juce::String categoryShort(juce::String name){auto n=name.toLowerCase();if(n=="closed hat")return "CH";if(n=="open hat")return "OH";if(n=="low tom")return "LT";if(n=="mid tom")return "MT";if(n=="high tom")return "HT";return name.toUpperCase().substring(0,6);}
inline juce::Colour tag(juce::String text) {
    static const juce::Colour colours[] {juce::Colour(0xff94b898),juce::Colour(0xff929ab9),juce::Colour(0xffb0a28e),juce::Colour(0xffb0919b),juce::Colour(0xff8aaeb3)};
    return colours[(unsigned)text.toLowerCase().hashCode()%5];
}
}
enum class Icon { Browser,Folder,Save,Auto,Play,Midi,Sequential,Random,Grid,Transient,Free,Quantize,Swing,Link,Minus,Plus,Left,Right,Reset,Recursive,Up,Star,Undo,Redo,Power,Lock,Trash };
class IconButton final : public juce::Button {
    Icon icon;
public:
    IconButton(Icon type,juce::String tip) : Button(tip),icon(type) { setTooltip(tip);setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics& g,bool hover,bool pressed) override {
        auto bounds = getLocalBounds().toFloat().reduced(1);bool on = getToggleState();
        g.setColour(on ? palette::accent().withAlpha(.18f) : juce::Colour(hover ? 0xff424242 : 0xff343434));g.fillRect(bounds);
        g.setColour(on ? palette::accent() : juce::Colour(0xff525252));g.drawRect(bounds,1);
        auto area = bounds.reduced(7);float x = area.getX(),y = area.getY(),w = area.getWidth(),h = area.getHeight();
        g.setColour(on ? palette::accent().brighter(.3f) : juce::Colours::white.withAlpha(isEnabled() ? .88f : .3f));
        if (pressed) g.setColour(palette::accent().brighter(.2f));
        auto line = [&](float x1,float y1,float x2,float y2) { g.drawLine(x1,y1,x2,y2,1.6f); };
        if((bool)getProperties().getWithDefault("bulkMixed",false)){g.drawLine(bounds.getCentreX()-4,bounds.getBottom()-3,bounds.getCentreX()+4,bounds.getBottom()-3,2);}
        switch (icon) {
            case Icon::Trash: line(x,y+h*.2f,x+w,y+h*.2f);line(x+w*.3f,y,x+w*.7f,y);g.drawRect(x+w*.15f,y+h*.25f,w*.7f,h*.75f,1.4f);line(x+w*.4f,y+h*.4f,x+w*.4f,y+h*.85f);line(x+w*.6f,y+h*.4f,x+w*.6f,y+h*.85f);break;
            case Icon::Lock: g.drawRect(x,y+h*.4f,w,h*.6f,1.4f);g.drawRoundedRectangle(x+w*.2f,y,w*.6f,h*.6f,3,1.4f);break;
            case Icon::Power: g.drawEllipse(x,y+h*.15f,w,h*.85f,1.4f);line(x+w*.5f,y,x+w*.5f,y+h*.5f);break;
            case Icon::Up: line(x+w*.5f,y+h,x+w*.5f,y);line(x,y+h*.4f,x+w*.5f,y);line(x+w,y+h*.4f,x+w*.5f,y);break;
            case Icon::Star: {juce::Path star;for(int i=0;i<10;++i){float a=i*juce::MathConstants<float>::pi/5, r=(i%2?.23f:.5f);float xx=x+w*.5f+std::sin(a)*w*r,yy=y+h*.5f-std::cos(a)*h*r;if(i==0)star.startNewSubPath(xx,yy);else star.lineTo(xx,yy);}star.closeSubPath();g.strokePath(star,juce::PathStrokeType(1.5f));break;}
            case Icon::Undo: case Icon::Redo: {bool right=icon==Icon::Redo;float a=right?x+w:x,b=right?x:x+w;line(a,y+h*.3f,b,y+h*.3f);line(a,y+h*.3f,a+(right?-1:1)*w*.3f,y);line(a,y+h*.3f,a+(right?-1:1)*w*.3f,y+h*.6f);line(b,y+h*.3f,b,y+h);break;}
            case Icon::Browser: g.drawRect(area,1.3f);line(x+w*.3f,y,x+w*.3f,y+h);break;
            case Icon::Folder: { juce::Path p;p.startNewSubPath(x,y+h*.2f);p.lineTo(x+w*.35f,y+h*.2f);p.lineTo(x+w*.48f,y+h*.38f);p.lineTo(x+w,y+h*.38f);p.lineTo(x+w,y+h);p.lineTo(x,y+h);p.closeSubPath();g.strokePath(p,juce::PathStrokeType(1.5f));break; }
            case Icon::Save: case Icon::Auto: g.drawRect(area,1.4f);g.drawRect(x+w*.2f,y,w*.55f,h*.35f,1.2f);g.drawRect(x+w*.2f,y+h*.6f,w*.6f,h*.4f,1.2f);if (icon==Icon::Auto) { g.setFont(8);g.drawText("A",(int)(x+w*.3f),(int)(y+h*.35f),(int)(w*.5f),(int)(h*.3f),juce::Justification::centred); }break;
            case Icon::Play: { juce::Path p;p.addTriangle(x,y,x+w,y+h*.5f,x,y+h);g.fillPath(p);break; }
            case Icon::Midi: g.drawRect(area,1.4f);for (int i=1;i<4;++i) line(x+w*i/4,y+h*.4f,x+w*i/4,y+h);g.fillRect(x+w*.2f,y,w*.16f,h*.55f);g.fillRect(x+w*.6f,y,w*.16f,h*.55f);break;
            case Icon::Sequential: for (int i=0;i<3;++i) { float yy=y+h*(i+.5f)/3;line(x,yy,x+w*.8f,yy);line(x+w*.65f,yy-h*.12f,x+w*.8f,yy);line(x+w*.65f,yy+h*.12f,x+w*.8f,yy); }break;
            case Icon::Random: line(x,y+h*.2f,x+w,y+h*.8f);line(x,y+h*.8f,x+w,y+h*.2f);line(x+w*.7f,y+h*.15f,x+w,y+h*.2f);line(x+w,y+h*.2f,x+w*.9f,y+h*.45f);line(x+w*.7f,y+h*.7f,x+w,y+h*.8f);break;
            case Icon::Grid: g.drawRect(area,1);for(int i=1;i<3;++i){line(x+w*i/3,y,x+w*i/3,y+h);line(x,y+h*i/3,x+w,y+h*i/3);}break;
            case Icon::Transient: line(x,y+h*.5f,x+w*.2f,y+h*.5f);line(x+w*.2f,y+h*.5f,x+w*.4f,y);line(x+w*.4f,y,x+w*.6f,y+h);line(x+w*.6f,y+h,x+w*.8f,y+h*.5f);line(x+w*.8f,y+h*.5f,x+w,y+h*.5f);break;
            case Icon::Free: {juce::Path p;p.startNewSubPath(x,y+h*.6f);p.cubicTo(x+w*.2f,y,x+w*.6f,y+h,x+w,y+h*.2f);g.strokePath(p,juce::PathStrokeType(1.7f));break;}
            case Icon::Quantize: for(int i=0;i<3;++i) line(x+w*i/2,y,x+w*i/2,y+h);g.fillEllipse(x+w*.36f,y+h*.4f,w*.28f,h*.28f);break;
            case Icon::Swing: g.fillEllipse(x,y+h*.6f,w*.2f,h*.25f);line(x+w*.16f,y+h*.7f,x+w*.16f,y);g.fillEllipse(x+w*.7f,y+h*.4f,w*.2f,h*.25f);line(x+w*.86f,y+h*.5f,x+w*.86f,y);line(x+w*.16f,y,x+w*.86f,y);break;
            case Icon::Link: g.drawRoundedRectangle(x,y+h*.1f,w*.65f,h*.4f,3,1.5f);g.drawRoundedRectangle(x+w*.35f,y+h*.5f,w*.65f,h*.4f,3,1.5f);break;
            case Icon::Minus: line(x,y+h*.5f,x+w,y+h*.5f);break;
            case Icon::Plus: line(x,y+h*.5f,x+w,y+h*.5f);line(x+w*.5f,y,x+w*.5f,y+h);break;
            case Icon::Left: line(x+w*.7f,y,x+w*.2f,y+h*.5f);line(x+w*.2f,y+h*.5f,x+w*.7f,y+h);break;
            case Icon::Right: line(x+w*.3f,y,x+w*.8f,y+h*.5f);line(x+w*.8f,y+h*.5f,x+w*.3f,y+h);break;
            case Icon::Reset: g.drawEllipse(area,1.5f);line(x,y+h*.25f,x,y+h*.6f);line(x,y+h*.25f,x+w*.35f,y+h*.25f);break;
            case Icon::Recursive: g.drawRect(x,y,w*.38f,h*.35f,1.3f);g.drawRect(x+w*.6f,y+h*.65f,w*.38f,h*.35f,1.3f);line(x+w*.2f,y+h*.35f,x+w*.2f,y+h*.8f);line(x+w*.2f,y+h*.8f,x+w*.6f,y+h*.8f);break;
        }
    }
};
class MidiDrag final : public juce::Component, public juce::SettableTooltipClient {
    bool dragged = false;juce::String title="MIDI";
public:
    std::function<void()> started;void notifyStarted(bool ok){if(ok){if(started)started();}else if(message)message("Could not start MIDI drag.");}
    std::function<juce::File()> prepare;std::function<void(juce::String)> message;
    void setTitle(juce::String value){title=value;repaint();}
    MidiDrag() { setMouseCursor(juce::MouseCursor::DraggingHandCursor);setTooltip("Drag the current trimmed, quantized/swung MIDI pattern into your DAW."); }
    void paint(juce::Graphics& g) override {g.setColour(juce::Colour(0xff383838));g.fillRect(getLocalBounds());g.setColour(isEnabled()?palette::accent():palette::muted());g.drawRect(getLocalBounds(),1);g.setFont(13);g.setColour(isEnabled()?juce::Colours::white:palette::muted());g.drawText(title,getLocalBounds(),juce::Justification::centred);}
    void mouseDown(const juce::MouseEvent&) override { dragged=false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if(dragged || e.getDistanceFromDragStart()<5 || !prepare)return;dragged=true;auto file=prepare();if(!file.existsAsFile())return;
        notifyStarted(juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,this));
    }
};

class AudioDrag final:public juce::Component,public juce::SettableTooltipClient{
 bool dragged=false,waiting=false;juce::File prepared;
public:
 std::function<void()> prepare,cancel;std::function<void(juce::String)> message;
 AudioDrag(){setMouseCursor(juce::MouseCursor::DraggingHandCursor);setTooltip("Drag rendered loop audio, including current playback settings, into your DAW.");}
 void paint(juce::Graphics& g)override{g.setColour(juce::Colour(0xff383838));g.fillRect(getLocalBounds());g.setColour(isEnabled()?palette::accent():palette::muted());g.drawRect(getLocalBounds());g.setColour(isEnabled()?juce::Colours::white:palette::muted());g.setFont(12);g.drawText(waiting?"…":"AUDIO",getLocalBounds(),juce::Justification::centred);}
 void mouseDown(const juce::MouseEvent&)override{dragged=false;waiting=false;prepared=juce::File();}
 void beginDrag(){if(!prepared.existsAsFile()||dragged)return;dragged=true;waiting=false;bool ok=juce::DragAndDropContainer::performExternalDragDropOfFiles({prepared.getFullPathName()},false,this);if(!ok&&message)message("Could not start audio drag.");repaint();}
 void rendered(juce::File file,juce::String error){if(!waiting)return;waiting=false;prepared=file;if(error.isNotEmpty()){if(message)message(error);}else if(juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())beginDrag();repaint();}
 void mouseDrag(const juce::MouseEvent& e)override{if(dragged||e.getDistanceFromDragStart()<5)return;if(prepared.existsAsFile()){beginDrag();return;}if(!waiting&&prepare){waiting=true;prepare();repaint();}}
 void mouseUp(const juce::MouseEvent&)override{if(waiting&&cancel)cancel();waiting=false;repaint();}
};

class ZoomBar final:public juce::Component,public juce::SettableTooltipClient{
    bool vertical=false;double begin=0,size=1,anchor=0,oldBegin=0,oldEnd=1;int drag=0;
public:explicit ZoomBar(bool isVertical=false):vertical(isVertical){}
    double axisLength()const{return juce::jmax(1,vertical?getHeight():getWidth());}double axis(const juce::MouseEvent& e)const{return vertical?e.y:e.x;}
    std::function<void(double,double)> changed;
    void setAutoHide(bool){}void setRangeLimits(double,double){}void addListener(juce::ScrollBar::Listener*){}void removeListener(juce::ScrollBar::Listener*){}void setSingleStepSize(double){}
    void setCurrentRange(double a,double b,juce::NotificationType){begin=a;size=b;repaint();}
    void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xff303030));float a=(float)(begin*axisLength()),w=(float)(size*axisLength());auto rect=[&](float point,float length){return vertical?juce::Rectangle<float>(0,point,(float)getWidth(),length):juce::Rectangle<float>(point,0,length,(float)getHeight());};g.setColour(juce::Colour(0xff686868));g.fillRect(rect(a,w));g.setColour(juce::Colour(0xffaaaaaa));g.fillRect(rect(a,3));g.fillRect(rect(a+w-3,3));}
    void resetZoom(){drag=0;begin=0;size=1;if(changed)changed(0,1);repaint();}
    void mouseDoubleClick(const juce::MouseEvent&)override{resetZoom();}
    void mouseDown(const juce::MouseEvent& e)override{if(e.mods.isAltDown()){resetZoom();return;}anchor=axis(e)/axisLength();oldBegin=begin;oldEnd=begin+size;double a=begin*axisLength(),b=(begin+size)*axisLength();drag=std::abs(axis(e)-a)<9?1:std::abs(axis(e)-b)<9?2:3;}
    void dragTo(double point){double a=oldBegin,b=oldEnd;if(drag==1)a=juce::jlimit(0.,b-1./64,point);else if(drag==2)b=juce::jlimit(a+1./64,1.,point);else{a=juce::jlimit(0.,1.-size,oldBegin+point-anchor);b=a+size;}begin=a;size=b-a;if(changed)changed(a,size);repaint();}
    void mouseDrag(const juce::MouseEvent& e)override{if(!drag)return;dragTo(axis(e)/axisLength());}void mouseUp(const juce::MouseEvent&)override{drag=0;}
    void mouseMove(const juce::MouseEvent& e)override{bool edge=std::abs(axis(e)-begin*axisLength())<9||std::abs(axis(e)-(begin+size)*axisLength())<9;setMouseCursor(edge?(vertical?juce::MouseCursor::UpDownResizeCursor:juce::MouseCursor::LeftRightResizeCursor):juce::MouseCursor::DraggingHandCursor);}
};

class SquareLook:public juce::LookAndFeel_V4{public:juce::Label* createSliderTextBox(juce::Slider& slider)override{return NumericInputFilter::sliderLabel(juce::LookAndFeel_V4::createSliderTextBox(slider));}void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool hover,bool down)override{if(b.getButtonText()=="M"||b.getButtonText()=="S"){g.setColour(b.findColour(b.getToggleState()?juce::TextButton::textColourOnId:juce::TextButton::textColourOffId));g.setFont(10);g.drawText(b.getButtonText(),b.getLocalBounds().reduced(1),juce::Justification::centred,false);}else juce::LookAndFeel_V4::drawButtonText(g,b,hover,down);}void drawComboBox(juce::Graphics& g,int width,int height,bool,int,int,int,int,juce::ComboBox& box)override{g.setColour(box.findColour(juce::ComboBox::backgroundColourId));g.fillRect(0,0,width,height);g.setColour(box.findColour(juce::ComboBox::outlineColourId));g.drawRect(0,0,width,height);juce::Path arrow;float x=width-13.f,y=height*.5f;arrow.startNewSubPath(x-4,y-2);arrow.lineTo(x,y+2);arrow.lineTo(x+4,y-2);g.setColour(box.findColour(juce::ComboBox::textColourId));g.strokePath(arrow,juce::PathStrokeType(1.5f));}void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& c,bool hover,bool down)override{g.setColour(hover||down?c.brighter(.1f):c);g.fillRect(b.getLocalBounds());g.setColour(juce::Colour(0xff555555));g.drawRect(b.getLocalBounds());}};

inline double wholeEvenBars(double bars,double factor){return juce::jmax(1.,std::floor(bars*factor/2.+1e-9)*2.);}
