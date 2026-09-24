import unittest

from srw64_native.catalog import signature
from srw64_native.translation import (DecodeError, Term, decode, encode, join_lines, missing_terms,
                                      record_problems, relevant_terms, style_problems)


class EncodeTests(unittest.TestCase):
    def test_pages_split_at_stop_and_lines_rejoin(self):
        e = encode('「いつまで、<BR> そうしている<STOP> もう師匠は<BR>　いない」<END>')
        self.assertEqual(e.pages, ['「いつまで、そうしている', 'もう師匠はいない」'])
        self.assertEqual(join_lines('FLYING<BR> IN<BR><BR>THE SKY<BR>　高く'), 'FLYING IN THE SKY高く')

    def test_name_runs_round_trip_exactly(self):
        source = '「<G:0126><G:0126><G:0126>、<G:012A><G:012A>!<STOP>そして<G:0104>」<END>'
        e = encode(source)
        self.assertEqual(e.pages, ['「【主角名字】、【搭档名字】!', 'そして⟦G1⟧」'])
        target = decode(e, ['“【主角名字】，【搭档名字】！', '然后⟦G1⟧”'])
        self.assertEqual(target, '“<G:0126><G:0126><G:0126>，<G:012A><G:012A>！<STOP>然后<G:0104>”<END>')
        self.assertEqual(signature(target), signature(source))

    def test_repeated_slot_keeps_each_original_run(self):
        e = encode('<G:0124><G:0124>と<G:0124><G:0124><G:0124><END>')
        self.assertEqual(decode(e, ['【主角昵称】和【主角昵称】']),
                         '<G:0124><G:0124>和<G:0124><G:0124><G:0124><END>')

    def test_structure_violations_are_errors(self):
        e = encode('「あ<STOP>い」<END>')
        with self.assertRaises(DecodeError):
            decode(e, ['“啊，是”'])
        e = encode('「<G:0126>と<G:012A>」<END>')
        with self.assertRaises(DecodeError):
            decode(e, ['“【搭档名字】和【主角名字】”'])  # order matters to compile_locale
        with self.assertRaises(DecodeError):
            decode(e, ['“【主角名字】”'])

    def test_angle_brackets_cannot_inject_tokens(self):
        target = decode(encode('あ<END>'), ['<STOP>'])
        self.assertEqual(target, '＜STOP＞<END>')

    def test_choice_options_stay_separate_lines(self):
        e = encode('シーラの方へ向かう<BR>エレの方へ向かう<END>', choice=True)
        self.assertEqual(e.pages, ['シーラの方へ向かう', 'エレの方へ向かう'])
        self.assertEqual(decode(e, ['前往希拉那边', '前往艾蕾那边']), '前往希拉那边<BR>前往艾蕾那边<END>')


class CheckTests(unittest.TestCase):
    def test_style_problems(self):
        self.assertIn('空译文', style_problems('「はい」', '【主角名字】'))
        self.assertTrue(any(p.startswith('残留假名') for p in style_problems('はい', '是ね')))
        self.assertIn('未转换的日文引号 」', style_problems('「はい」', '“是」'))
        self.assertEqual(style_problems('「はい、わかりました」', '“是，明白了”'), [])

    def test_quotes_balance_over_the_whole_record(self):
        self.assertEqual(record_problems(['“第一页', '第二页”']), [])
        self.assertEqual(record_problems(['“第一页', '第二页']), ['双引号不成对'])

    def test_terms_prefer_longest_and_respect_katakana_words(self):
        terms = [Term('ガイ', '盖', 't', True), Term('ガイゾック', '盖佐克', 't', True),
                 Term('ロームフェラ財団', '罗姆菲拉财团', 't', True), Term('ロームフェラ', '罗姆菲拉', 't', True)]
        hits = [t.ja for t in relevant_terms(['ガイゾックが来た。ロームフェラ財団も'], terms)]
        self.assertEqual(hits, ['ロームフェラ財団', 'ガイゾック'])
        self.assertEqual(missing_terms(['ガイゾックだ'], ['是盖佐克'], terms), [])
        self.assertEqual(missing_terms(['ガイゾックだ'], ['是加佐克'], terms), ['ガイゾック→盖佐克'])


class EnglishCheckTests(unittest.TestCase):
    def test_english_rejects_leftover_cjk_and_fullwidth_punctuation(self):
        problems = style_problems('「はい、わかりました」', '“Yes, 了解。”', 'en')
        self.assertTrue(any(p.startswith('残留汉字') for p in problems))
        self.assertTrue(any(p.startswith('残留全角标点') for p in problems))
        self.assertEqual(style_problems('「はい、わかりました」', '“Yes, understood.”', 'en'), [])

    def test_english_rejects_romanized_honorifics(self):
        problems = style_problems('「万丈様の執事です」', '“I am Banjo-sama’s butler.”', 'en')
        self.assertTrue(any(p.startswith('保留了日式敬称') for p in problems))
        self.assertEqual(style_problems('「アル＝イー＝クイスだ」', '“It’s the Al-E-Quis!”', 'en'), [])

    def test_english_apostrophes_do_not_unbalance_quotes(self):
        self.assertEqual(record_problems(['“Don’t go,', 'it’s a trap!”'], 'en'), [])
        self.assertEqual(record_problems(['"Don’t go!"'], 'en'), ['用了直引号，应为 “ ”'])

    def test_terms_match_case_insensitively(self):
        terms = [Term('ゲッタービーム', 'Getter Beam', 't', True)]
        self.assertEqual(missing_terms(['ゲッタービーム!'], ['“GETTER BEAM!”'], terms), [])


class RatioTests(unittest.TestCase):
    def test_drawn_out_katakana_shouts_are_short(self):
        from srw64_native.translation import ratio_problems
        self.assertEqual(ratio_problems(['「ライトニングソォォォォォード!!」'], ['“闪电剑！！”']), [])
        self.assertEqual(ratio_problems(['「…………」'], ['“...”'], 'en'), [])
        long = '「帝国上層部とスペシャルズの幹部がレジスタンス対策のために集まるというのが本当ならば」'
        self.assertTrue(ratio_problems([long], ['“如果属实”']))


class QuoteNormalizationTests(unittest.TestCase):
    def test_one_pair_across_pages(self):
        from srw64_native.translation import normalize_quotes
        src = ['「なに!?', 'ばかな」']
        self.assertEqual(normalize_quotes(src, ['“什么！？”', '“怎么可能”']), ['“什么！？', '怎么可能”'])
        self.assertEqual(normalize_quotes(src, ['什么！？', '怎么可能'], 'en'), ['“什么！？', '怎么可能”'])
        self.assertEqual(normalize_quotes(['（あいつめ……）'], ['“That guy...”'], 'en'), ['(That guy...)'])
        self.assertEqual(normalize_quotes(['（あいつめ……）'], ['（那家伙……）']), ['（那家伙……）'])

    def test_other_shapes_are_left_alone(self):
        from srw64_native.translation import normalize_quotes
        two = ['「行くぞ」「おう」']
        self.assertEqual(normalize_quotes(two, ['“走”“好”']), ['“走”“好”'])
        self.assertEqual(normalize_quotes(['ナレーション'], ['旁白']), ['旁白'])

    def test_renames_whole_names_only(self):
        from srw64_native.translation import Renames
        zh = Renames({"阿克": "亚克", "格拉多斯": "古拉特斯"}, protected=["阿克西斯"])
        self.assertEqual(zh.apply("阿克和阿克西斯，格拉多斯兵"), "亚克和阿克西斯，古拉特斯兵")
        self.assertEqual(zh.apply("【主角名字】说阿克"), "【主角名字】说亚克")
        en = Renames({"Ark": "Arc", "Devil Gundam": "Dark Gundam"}, protected=["Arklight"], latin=True)
        self.assertEqual(en.apply("Ark, Arklight and the Devil Gundam. Darkness."), "Arc, Arklight and the Dark Gundam. Darkness.")
        self.assertEqual(en.apply("{HeroName} met Ark"), "{HeroName} met Arc")
        gated = Renames({"老大": "波士"}, gates={"老大": {"ボス"}})
        self.assertEqual(gated.apply("老大，走吧", source="ボス、行くぞ"), "波士，走吧")
        self.assertEqual(gated.apply("老大，走吧", source="親分、行くぞ"), "老大，走吧")
        closed = Renames({"德尔麦尤公爵": "迪鲁马尤公爵", "德尔麦尤": "迪鲁马尤"},
                         gates={"德尔麦尤公爵": {"デルマイユ公爵"}, "德尔麦尤": {"デルマイユ"}})
        self.assertEqual(closed.apply("请留步，德尔麦尤公爵。", source="お待ちください、デルマイユ公"), "请留步，迪鲁马尤公爵。")
        split = Renames({"兹尔皇帝": "祖鲁皇帝"}, gates={"兹尔皇帝": {"ズール皇帝"}})
        self.assertEqual(split.apply("兹尔皇帝的东西", source="ズール<BR> 皇帝のもの"), "祖鲁皇帝的东西")
        robo = Renames({"巨大机器人": "大铁人"}, gates={"巨大机器人": {"ジャイアント・ロボ"}})
        self.assertEqual(robo.apply("巨大机器人被击破", source="ジャイアントロボの撃破"), "大铁人被击破")

    def test_chinese_marks(self):
        from srw64_native.translation import chinese_marks
        self.assertEqual(chinese_marks(['“东方不败!', '什么!?”'], 'zh-Hans'), ['“东方不败！', '什么！？”'])
        self.assertEqual(chinese_marks(['我竟然……。给我记住', '前辈……。”'], 'zh-Hans'), ['我竟然……给我记住', '前辈……”'])
        self.assertEqual(chinese_marks(['What?!...'], 'en'), ['What?!...'])
