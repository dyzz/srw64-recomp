#include "native_dialogue.hpp"
#include "text/portable_text.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

namespace srw64::dialogue {
std::shared_ptr<const Frame> presented_frame(uint64_t) {return {};}
}
using namespace srw64::dialogue;
int main() {
    // Portable game text: long Unicode, mixed punctuation, kana and a
    // combining character must survive wrapping and pagination without loss.
    const auto text=utf16("玛娜米说：“这是完整译文，不受旧字格限制。”\nABC 123 é が。这是第二段较长的文字，应该自动分页，并保持全部字符。");
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
    reader.begin(1,17412,u"劳伦斯",long_text,0);
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
    reader.begin(2,17412,u"劳伦斯",typeset(u"下一句。",13),101);
    reader.history.back().localized={{"ja",u"まだ読んでいない長い会話"},{"zh-Hans",u"下一句。"}};
    reader.history.back().text=u"下";
    const auto event_before=reader.event;
    const auto history_before=reader.history.size();
    reader.switch_language(typeset(u"まだ読んでいない長い会話",13),{},"ja",101);
    assert(reader.event==event_before && reader.history.size()==history_before && !reader.pending);
    assert(reader.history.front().text==u"完了した会話");
    assert(reader.history.back().text.empty() && !reader.history.back().complete);
    assert(reader.page==0 && reader.visible==0 && !reader.auto_read && !reader.skipping);
    reader.switch_language(typeset(u"下一句。",13),{},"zh-Hans",101);
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
    reader.begin(3,17413,u"玛娜米",typeset(u"新的剧情段。",13),183);
    reader.update(Reader::R|Reader::START,184);
    assert(!reader.skipping); // Chord held across a boundary is not re-armed.
    reader.update(0,185);
    reader.update(Reader::BIGGER,186);
    assert(reader.font_size==14);
    reader.relayout(typeset(reader.layout.text,reader.font_size));
    assert(reader.page<reader.layout.pages.size());
    // Revealing a combining sequence never cuts its UTF-16 representation.
    reader.begin(4,100,u"测试",typeset(utf16("éが甲"),13),200);
    reader.update(0,202);assert(reader.visible==2);
    reader.update(0,204);assert(reader.visible==4);
    assert(reader.history.back().text==utf16("éが"));
    // Fast-forward must stop on release; an A used to dismiss history cannot
    // confirm a guest line, including while automatic reading is enabled.
    Reader fast_reader;
    fast_reader.begin(1,100,u"测试",typeset(u"第一句。",13),0);
    assert(fast_reader.update(Reader::R|Reader::A,6));
    fast_reader.begin(2,101,u"测试",typeset(u"第二句。",13),7);
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
    // A host notice (an upgrade refund) goes before the fragment being read, keeps
    // every language, and does not take part in the speaker colours.
    Reader noted;
    noted.begin(1,100,u"甲",typeset(u"第一句。",13),0);
    noted.begin(2,101,u"乙",typeset(u"第二句。",13),2);
    assert(noted.history.back().warm_name);
    noted.note(99,{{"ja",u"返金"},{"zh-Hans",u"退款"}},"zh-Hans");
    assert(noted.history.size()==3 && noted.history.back().event==2);
    assert(noted.history[1].notice && noted.history[1].complete && noted.history[1].text==u"退款");
    noted.boundary();
    noted.note(100,{{"zh-Hans",u"退款二"}},"zh-Hans");
    assert(noted.history.back().notice && noted.history.back().event==100);
    noted.begin(3,102,u"甲",typeset(u"第三句。",13),4);
    assert(!noted.history.back().warm_name);  // 甲 after 乙, as if no notice were there
    noted.switch_language(typeset(u"第三句。",13),{},"ja",5);
    assert(noted.history[1].text==u"返金" && noted.history[3].text.empty());
    // Speaker names follow the language too; an entry without them keeps its name.
    Reader named;
    named.begin(1,100,u"ローレンス",typeset(u"一句。",13),0);
    named.history.back().localized_speaker={{"ja",u"ローレンス"},{"en",u"Lawrence"}};
    named.begin(2,101,u"マナミ",typeset(u"二句。",13),2);
    named.switch_language(typeset(u"二句。",13),{},"en",3);
    assert(named.history[0].speaker==u"Lawrence" && named.history[1].speaker==u"マナミ");
    // A long translation must not make the automatic delay on its FIRST page
    // depend on the length of all unread pages that follow it.
    Reader automatic;
    automatic.begin(1,100,u"测试",typeset(std::u16string(2000,u'甲'),18),0);
    automatic.update(Reader::UP,2);
    for(unsigned vi=4;vi<240;vi+=2)automatic.update(0,vi);
    assert(automatic.page>0 && !automatic.pending);
    // Progress must describe the exact deadline used by automatic advance,
    // including typewriter time, and freeze while history owns the input.
    Reader progress;
    progress.begin(1,100,u"测试",typeset(u"等待下一句。",13),0);
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
    progress.begin(2,101,u"下一位",typeset(u"下一句。",13),1000);
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
    paged_progress.begin(1,100,u"测试",long_text,0);
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
    for(unsigned i=5;i<300;++i)reader.begin(i,100,u"测试",typeset(u"甲",13),i+200);
    assert(reader.history.size()==Reader::history_limit);
    // A story record is read whole: its original pages joined, directly in
    // Chinese and with a space in English, and where each later page starts.
    {
        const auto zh=joined_record(u"第一页。\f第二页。\f",
            "zh-Hans");
        assert(zh.text==u"第一页。第二页。" && (zh.stops==std::vector<size_t>{4,8}));
        const auto en=joined_record(u"Hello.\fWorld\n\fAgain",
            "en");
        assert(en.text==u"Hello. World\nAgain" && (en.stops==std::vector<size_t>{7,13}));
    }
    // The text area under the name row holds three Chinese lines at the
    // default size, spread over its height; English uses 0.85 of the setting.
    {
        const auto story=utf16("但大半殖民卫星被毁，地上也遭受了巨大损失。人口锐减，驱动地球圈运转的国家也大多失去了力量。于是联邦政府接管了一切，一个巨大的统一国家诞生了。");
        const auto body=typeset_body(story,body_size(13));
        assert(body.pages.size()>1 && body.pages.front().lines.size()==3);
        assert(std::abs(body.shaped->line_height()-body_height/3)<1e-9);
        for(const auto& page:body.pages)assert(page.lines.size()<=3);
        assert(typeset_body(story,body_size(14)).pages.front().lines.size()==2);
        auto english=std::make_shared<srw64::localization::Catalog>();english->locale="en";
        srw64::localization::Scope scope(english);
        assert(std::abs(body_size(13)-11.05)<1e-9);
        const auto words=typeset_body(u"Most of the colonies were destroyed, and the Earth suffered great losses. The population fell sharply.",body_size(13));
        assert(words.pages.front().lines.size()==3);
    }
    // The original's pages are confirmed behind the reader, one A at a time,
    // as soon as a host page reaches them; <END> follows the last host page.
    {
        const auto record=joined_record(u"甲。\f乙。\f丙。","zh-Hans");
        const auto layout=typeset_body(record.text,body_size(13),record.stops);
        assert(layout.pages.size()==1);
        Reader story;
        story.begin(1,100,u"甲",layout,0,record.stops);
        assert(story.required_guest()==2);
        assert(story.confirmation(1)==Confirm::stop);story.guest=1;
        assert(story.confirmation(2)==Confirm::stop);story.guest=2;
        assert(story.confirmation(3)==Confirm::none);
        assert(story.update(Reader::A,10) && story.pending);
        assert(story.confirmation(11)==Confirm::end && story.confirmation(12)==Confirm::none);
        // Skipping ahead of the original: its pages still go one by one, and
        // an A the original ignored is sent again after the retry delay.
        Reader ahead;
        ahead.begin(2,100,u"甲",layout,0,record.stops);
        ahead.update(Reader::R|Reader::START,1);assert(ahead.skipping && ahead.pending);
        assert(ahead.confirmation(2)==Confirm::stop && ahead.confirmation(3)==Confirm::none);
        assert(ahead.confirmation(2+Reader::stop_retry_vis)==Confirm::stop);
        ahead.guest=1;assert(ahead.confirmation(40)==Confirm::stop);
        ahead.guest=2;assert(ahead.confirmation(41)==Confirm::end && ahead.confirmation(42)==Confirm::none);
        // History open: nothing is confirmed while the game waits.
        Reader paused;
        paused.begin(3,100,u"甲",layout,0,record.stops);
        paused.update(Reader::L,1);assert(paused.history_open && paused.confirmation(2)==Confirm::none);
        // The typewriter rests 0.3 s at each original page break in a host page.
        Reader typing;
        typing.begin(4,100,u"甲",layout,0,record.stops);
        const auto& page=typing.layout.pages[0];
        assert(typing.revealed_at(page,4,30)==2 && typing.revealed_at(page,4+Reader::stop_pause_vis-1,30)==2);
        assert(typing.revealed_at(page,4+Reader::stop_pause_vis+2,30)==3);
        assert(typing.page_timing().reveal_vis==12+2*Reader::stop_pause_vis);
    }
    // A long record over several host pages: every original page and <END>
    // are confirmed exactly once, never before the reader's page reaches them.
    {
        const auto record=joined_record(u"战争结束了。\f但大半殖民卫星被毁，地上也遭受了巨大损失。人口锐减，驱动地球圈运转的国家也大多失去了力量。\f"
            u"于是联邦政府接管了一切，一个巨大的统一国家诞生了。\f可是，那并不是和平的开始，而是新的战争的序曲。","zh-Hans");
        const auto layout=typeset_body(record.text,body_size(13),record.stops);
        assert(layout.pages.size()>=3);
        Reader story;
        story.begin(1,100,u"甲",layout,0,record.stops);
        unsigned stops_sent=0,ends=0;
        for(uint64_t vi=1;vi<400 && !ends;++vi) {
            const auto confirm=story.confirmation(vi);
            if(confirm==Confirm::stop){++stops_sent;++story.guest;}
            if(confirm==Confirm::end)++ends;
            assert(story.guest<=(story.pending?story.stops.size():story.required_guest()));
            story.update(vi%8==0?Reader::A:0,vi);
        }
        assert(stops_sent==record.stops.size() && ends==1);
        // A new font size keeps the current page's first character a page start.
        Reader sized;
        sized.begin(2,100,u"甲",layout,0,record.stops);
        sized.update(0,1);sized.update(Reader::A,2);assert(sized.page==1);
        const auto start=sized.page_start();
        sized.font_size=18;
        sized.relayout(typeset_body(sized.layout.text,body_size(18),sized.stops,{start}));
        assert(sized.page_start()==start && sized.layout.pages.size()>layout.pages.size());
        // Another language resumes at the original page the game shows.
        const auto other=joined_record(u"The war was over.\fBut most of the colonies were destroyed, and the Earth suffered great losses.\f"
            u"So the Federation took control of everything.\fYet that was not the start of peace, but the prelude to a new war.","en");
        auto english=std::make_shared<srw64::localization::Catalog>();english->locale="en";
        srw64::localization::Scope scope(english);
        Reader switched;
        switched.begin(3,100,u"甲",layout,0,record.stops);
        switched.history.back().localized={{"en",other.text}};
        switched.guest=2;switched.visible=layout.pages[0].end;switched.remember();
        switched.switch_language(typeset_body(other.text,body_size(13),other.stops,{other.stops[1]}),other.stops,"en",50);
        assert(switched.page_start()==other.stops[1] && switched.visible==other.stops[1]);
        assert(switched.history.back().text==other.text.substr(0,other.stops[1]));
        assert(switched.guest<=switched.required_guest());
    }
    std::cout<<"native dialogue: Core Text layout, Unicode clusters, pagination, controls, boundaries, history, advance timing and speaker focus passed\n";
}
