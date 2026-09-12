#include "native_dialogue.hpp"
#include <cassert>
#include <iostream>

namespace srw64::dialogue {
std::shared_ptr<const Frame> presented_frame(uint64_t) {return {};}
}
using namespace srw64::dialogue;
int main() {
    // Real Core Text: long Unicode, mixed punctuation, a surrogate pair and a
    // combining character must survive wrapping and pagination without loss.
    const auto text=utf16("玛娜米说：“这是完整译文，不受旧字格限制。”\nABC 123 é 🚀。这是第二段较长的文字，应该自动分页，并保持全部字符。");
    for(unsigned size:{10U,13U,18U}) {
        const auto layout=typeset(text,size);
        size_t end=0;
        for(const auto& page:layout.pages) {
            assert(page.start==end);
            for(const auto& line:page.lines) {
                assert(line.start==end && line.end>line.start);
                assert(line.width<=178);
                assert(std::binary_search(layout.clusters.begin(),layout.clusters.end(),line.end));
                end=line.end;
            }
            assert(page.end==end);
        }
        assert(end==text.size());assert(layout.pages.size()>1);
        assert(utf16(utf8(text))==text);
    }
    Reader reader;
    auto long_text=typeset(text,18);
    const auto pages=long_text.pages.size();
    reader.begin(1,17412,0,u"劳伦斯",long_text,0);
    // One confirmation per page; holding plain A cannot consume the next page.
    for(size_t i=0;i<pages;++i) {
        const auto result=reader.update(Reader::A,20+i*4);
        assert(result==(i+1==pages));
        assert(!reader.update(Reader::A,21+i*4));
        assert(!reader.update(0,22+i*4));
    }
    assert(reader.pending);
    assert(reader.history.back().complete);
    reader.history.back().localized={{"ja",u"完了した会話"},{"zh-Hans",text}};
    assert(!reader.update(Reader::A,100)); // Waiting for guest acknowledgement.
    reader.begin(2,17412,1,u"劳伦斯",typeset(u"下一句。",13),101);
    reader.history.back().localized={{"ja",u"まだ読んでいない長い会話"},{"zh-Hans",u"下一句。"}};
    reader.history.back().text=u"下";
    const auto event_before=reader.event;
    const auto history_before=reader.history.size();
    reader.switch_language(typeset(u"まだ読んでいない長い会話",13),"ja",101);
    assert(reader.event==event_before && reader.history.size()==history_before && !reader.pending);
    assert(reader.history.front().text==u"完了した会話");
    assert(reader.history.back().text.empty() && !reader.history.back().complete);
    assert(reader.page==0 && reader.visible==0 && !reader.auto_read && !reader.skipping);
    reader.switch_language(typeset(u"下一句。",13),"zh-Hans",101);
    assert(reader.page==0 && !reader.pending && reader.visible==0);
    reader.update(0,102);
    reader.update(Reader::UP,103);
    assert(reader.auto_read && reader.speed==1);
    reader.update(Reader::UP,110);assert(reader.speed==1);
    reader.update(Reader::UP,121);assert(reader.speed==2);
    reader.update(Reader::L,122);assert(reader.history_open);
    const auto visible=reader.visible;
    reader.update(Reader::R|Reader::A,180);
    assert(!reader.history_open && !reader.pending); // Closing cannot advance.
    assert(reader.visible==visible);
    reader.update(0,181);
    reader.update(Reader::R|Reader::START,182);
    assert(reader.skipping && reader.pending);
    reader.boundary();
    assert(!reader.skipping && !reader.auto_read && !reader.active);
    reader.begin(3,17413,0,u"玛娜米",typeset(u"新的剧情段。",13),183);
    reader.update(Reader::R|Reader::START,184);
    assert(!reader.skipping); // Chord held across a boundary is not re-armed.
    reader.update(0,185);
    reader.update(Reader::BIGGER,186);
    assert(reader.font_size==14);
    reader.relayout(typeset(reader.layout.text,reader.font_size));
    assert(reader.page<reader.layout.pages.size());
    // Revealing a combining sequence never cuts its UTF-16 representation.
    reader.begin(4,100,0,u"测试",typeset(utf16("é🚀甲"),13),200);
    reader.update(0,202);assert(reader.visible==2);
    reader.update(0,204);assert(reader.visible==4);
    assert(reader.history.back().text==utf16("é🚀"));
    // Fast-forward must stop on release; an A used to dismiss history cannot
    // confirm a guest line, including while automatic reading is enabled.
    Reader fast_reader;
    fast_reader.begin(1,100,0,u"测试",typeset(u"第一句。",13),0);
    assert(fast_reader.update(Reader::R|Reader::A,6));
    fast_reader.begin(2,101,0,u"测试",typeset(u"第二句。",13),7);
    assert(!fast_reader.update(0,20));
    assert(!fast_reader.fast && !fast_reader.pending);
    fast_reader.update(Reader::UP,21);
    fast_reader.update(Reader::L,22);
    for(unsigned vi=24;vi<800;vi+=2)assert(!fast_reader.update(0,vi));
    assert(fast_reader.history_open && !fast_reader.pending);
    assert(!fast_reader.update(Reader::A,800));
    assert(!fast_reader.history_open && !fast_reader.pending);
    fast_reader.update(Reader::B,802);
    assert(!fast_reader.auto_read);
    fast_reader.update(0,804);
    assert(fast_reader.update(Reader::A,806));
    assert(fast_reader.history.back().text==u"第二句。");
    // A long translation must not make the automatic delay on its FIRST page
    // depend on the length of all unread pages that follow it.
    Reader automatic;
    automatic.begin(1,100,0,u"测试",typeset(std::u16string(2000,u'甲'),18),0);
    automatic.update(Reader::UP,2);
    for(unsigned vi=4;vi<240;vi+=2)automatic.update(0,vi);
    assert(automatic.page>0 && !automatic.pending);
    // Progress must describe the exact deadline used by automatic advance,
    // including typewriter time, and freeze while history owns the input.
    Reader progress;
    progress.begin(1,100,0,u"测试",typeset(u"等待下一句。",13),0);
    progress.update(Reader::UP,2);progress.update(0,4);
    const auto deadline=progress.page_timing().total_vis();
    unsigned previous_progress=0;
    for(uint64_t vi=6;vi<deadline;vi+=2) {
        assert(!progress.update(0,vi));
        const auto value=progress.advance_progress();
        assert(value.visible && value.permille>=previous_progress && value.permille<1000);
        previous_progress=value.permille;
    }
    assert(progress.update(0,deadline+deadline%2));
    assert(progress.advance_progress().permille==1000);
    progress.begin(2,101,0,u"下一位",typeset(u"下一句。",13),1000);
    assert(progress.advance_progress().permille==0);
    progress.update(0,1002);progress.update(Reader::L,1004);
    const auto held_progress=progress.advance_progress().permille;
    for(uint64_t vi=1006;vi<1400;vi+=2) {
        assert(!progress.update(0,vi));
        assert(progress.advance_progress().paused && progress.advance_progress().permille==held_progress);
    }
    progress.update(Reader::L,1400);
    assert(!progress.history_open && progress.advance_progress().permille==held_progress);
    progress.update(Reader::B,1402);assert(!progress.advance_progress().visible);
    progress.update(0,1404);progress.update(Reader::UP,1406);
    assert(progress.advance_progress().visible);
    progress.update(Reader::R|Reader::A,1408);assert(!progress.advance_progress().visible);
    progress.boundary();assert(!progress.advance_progress().visible);
    Reader paged_progress;
    paged_progress.begin(1,100,0,u"测试",long_text,0);
    paged_progress.update(Reader::UP,2);
    for(uint64_t vi=4;paged_progress.page==0;vi+=2) {
        assert(vi<1000);assert(!paged_progress.update(0,vi));
    }
    assert(paged_progress.advance_progress().permille==0);
    Frame focus;
    focus.reading_event=1;focus.boxes[0].event=1;
    focus.boxes[0].visible=focus.boxes[0].active=true;
    focus.boxes[1].visible=true;focus.boxes[1].event=2;
    assert(focus.focused_box()==&focus.boxes[0]);
    focus.boxes[0].active=false; // Guest speaker handoff keeps the last speaker.
    assert(focus.focused_box()==&focus.boxes[0]);
    focus.boxes[1].active=true;assert(focus.focused_box()==&focus.boxes[1]);
    focus.boxes[0].active=true;assert(!focus.focused_box()); // Ambiguous state.
    focus.boxes[0].visible=focus.boxes[1].visible=false;assert(!focus.focused_box());
    for(unsigned i=5;i<300;++i)reader.begin(i,100,0,u"测试",typeset(u"甲",13),i+200);
    assert(reader.history.size()==Reader::history_limit);
    std::cout<<"native dialogue: Core Text layout, Unicode clusters, pagination, controls, boundaries, history, advance timing and speaker focus passed\n";
}
