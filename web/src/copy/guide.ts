// Page chrome for the guide pages, taken from the offline guide's builder
// (tools/content/build_guide.py, UI); the content itself is guide/data/<language>/.
import type { Lang } from '../i18n';

export const GUIDE_UI = {
  "zh": {
    "switch": "中文",
    "title": "《超级机器人大战64》攻略：流程、隐藏要素与资料",
    "nav": "攻略页面",
    "flow": "流程图",
    "hidden": "隐藏要素",
    "flow_lead": "关卡按话数排列，同一话数下并排展示不同主角或路线的关卡。话数根据游戏脚本的关卡衔接推算，采用各路线最常见的值；实际话数可能因路线相差几话，具体差异见卡片首条备注。",
    "hidden_lead": "人物、机体、武器与路线分歧的达成条件，根据本项目对游戏脚本的解析整理。与 Akurasu 攻略不同的地方已逐条标注。",
    "legend": [
      "加入",
      "临时参战",
      "离队",
      "强化",
      "部件",
      "隐藏要素"
    ],
    "tags": {
      "diff": "与 Akurasu 不同",
      "new": "Akurasu 未载",
      "unverified": "未实机验证"
    },
    "route_map": "路线总览",
    "victory": "胜利",
    "defeat": "败北",
    "after": "过关后：",
    "b_join": "加入／取得",
    "b_temp": "临时参战",
    "b_leave": "离队／无法使用",
    "b_upgrade": "强化／换机／新武器",
    "b_parts": "强化部件（击坠掉落）",
    "b_secret": "隐藏要素与分歧",
    "b_notes": "备注",
    "stage": [
      "第",
      "话"
    ],
    "step": "第 {} 话",
    "all": "全部",
    "akurasu_box": "与 Akurasu 的差异",
    "sources": "资料来源：",
    "sep": "；",
    "paren": "（{}）",
    "map": [
      [
        "开局",
        "四主角·真实／超级系"
      ],
      [
        "共通路线",
        "至破晓作战"
      ],
      [
        "OZ 路线",
        "破晓作战选 1"
      ],
      [
        "独立军路线",
        "破晓作战选 2"
      ],
      [
        "罗姆斐拉／月球",
        "多鲁基斯被毁二选一"
      ],
      [
        "独立军",
        "梁山泊之战"
      ],
      [
        "完全和平路线",
        "重返战场时二选一"
      ],
      [
        "OZ 后半",
        "乞力马扎罗的风暴起"
      ],
      [
        "独立军后半",
        "银河帝国军先遣舰队起"
      ],
      [
        "终章",
        "穆格宇宙／地球圈分头后汇合"
      ]
    ]
  },
  "ja": {
    "switch": "日本語",
    "title": "スーパーロボット大戦64 攻略：フローチャート・隠し要素・データ",
    "nav": "攻略ページ",
    "flow": "フローチャート",
    "hidden": "隠し要素",
    "flow_lead": "話数順に並べています。同じ話数に並ぶカードは、主人公やルートによって異なるシナリオです。話数はゲームスクリプトのシナリオ接続から算出し、各ルートで最も多い値を採用しています。ルートによって実際の話数が数話前後する場合は、カードの最初の備考に記載しています。",
    "hidden_lead": "隠しキャラクター・機体・武器とルート分岐の条件を、本プロジェクトによるゲームスクリプトの解析をもとにまとめています。海外攻略サイト Akurasu の情報と異なる点は、項目ごとに記載しています。",
    "legend": [
      "加入",
      "スポット参戦",
      "離脱",
      "強化",
      "パーツ",
      "隠し要素"
    ],
    "tags": {
      "diff": "Akurasu と相違",
      "new": "Akurasu 未記載",
      "unverified": "実機未確認"
    },
    "route_map": "ルート概要",
    "victory": "勝利",
    "defeat": "敗北",
    "after": "クリア後：",
    "b_join": "加入・入手",
    "b_temp": "スポット参戦",
    "b_leave": "離脱・使用不可",
    "b_upgrade": "強化・乗り換え・新武器",
    "b_parts": "強化パーツ（撃墜で入手）",
    "b_secret": "隠し要素と分岐",
    "b_notes": "備考",
    "stage": [
      "第",
      "話"
    ],
    "step": "第{}話",
    "all": "すべて",
    "akurasu_box": "Akurasu との違い",
    "sources": "出典：",
    "sep": "／",
    "paren": "（{}）",
    "map": [
      [
        "序盤",
        "主人公4人・リアル／スーパー系"
      ],
      [
        "共通ルート",
        "オペレーション・デイブレイクまで"
      ],
      [
        "OZルート",
        "デイブレイクで選択1"
      ],
      [
        "独立軍ルート",
        "デイブレイクで選択2"
      ],
      [
        "ロームフェラ／月",
        "トールギス破壊で二択"
      ],
      [
        "独立軍",
        "梁山泊の戦い"
      ],
      [
        "完全平和ルート",
        "戦場に帰るで二択"
      ],
      [
        "OZ後半",
        "キリマンジャロの嵐から"
      ],
      [
        "独立軍後半",
        "銀河帝国軍先遣艦隊から"
      ],
      [
        "最終章",
        "ムゲ宇宙／地球圏に分かれて合流"
      ]
    ]
  },
  "en": {
    "switch": "English",
    "title": "Super Robot Wars 64 Guide: Flow Chart, Secrets and Reference",
    "nav": "Guide pages",
    "flow": "Flow Chart",
    "hidden": "Secrets",
    "flow_lead": "Stages are listed by number, with different protagonists or routes shown side by side. Stage numbers are calculated from the connections in the game’s scripts, using the most common number across routes. Numbers may vary by a few stages depending on your route; the first note on each card explains the difference.",
    "hidden_lead": "Requirements for hidden pilots, units, weapons and route splits, based on this project’s analysis of the game’s stage scripts. Differences from the Akurasu guide are marked individually.",
    "legend": [
      "Joins",
      "Guest",
      "Leaves",
      "Upgrade",
      "Parts",
      "Secrets"
    ],
    "tags": {
      "diff": "Differs from Akurasu",
      "new": "Not on Akurasu",
      "unverified": "Not verified in game"
    },
    "route_map": "Routes at a glance",
    "victory": "Victory",
    "defeat": "Defeat",
    "after": "After the stage: ",
    "b_join": "Joins / obtained",
    "b_temp": "Guest units",
    "b_leave": "Leaves / unavailable",
    "b_upgrade": "Upgrades / unit changes / new weapons",
    "b_parts": "Parts (dropped by defeated units)",
    "b_secret": "Secrets and route splits",
    "b_notes": "Notes",
    "stage": [
      "Stage",
      ""
    ],
    "step": "Stage {}",
    "all": "All",
    "akurasu_box": "Differences from Akurasu",
    "sources": "Sources: ",
    "sep": "; ",
    "paren": " ({})",
    "map": [
      [
        "Opening",
        "Four protagonists · Real / Super"
      ],
      [
        "Common route",
        "Up to Operation Daybreak"
      ],
      [
        "OZ route",
        "Daybreak: choice 1"
      ],
      [
        "Independence Army",
        "Daybreak: choice 2"
      ],
      [
        "Romefeller / Moon",
        "Tallgeese Destroyed: pick one"
      ],
      [
        "Independence Army",
        "Battle of Ryozanpaku"
      ],
      [
        "Complete Pacifism",
        "Back to the Battlefield: pick one"
      ],
      [
        "OZ, second half",
        "From Kilimanjaro"
      ],
      [
        "Independence Army, second half",
        "From the Galactic Vanguard"
      ],
      [
        "Finale",
        "Split: Muge space / Earth, then rejoin"
      ]
    ]
  }
} as const;

export const MAP_BOXES: [string, number, number, number][] = [["rm-box", 10, 95, 140], ["rm-box", 190, 95, 140], ["rm-oz", 376, 25, 124], ["rm-ia", 376, 130, 124], ["rm-oz", 536, 25, 124], ["rm-ia", 536, 130, 124], ["rm-cp", 536, 200, 128], ["rm-oz", 702, 35, 124], ["rm-ia", 702, 130, 124], ["rm-end", 866, 95, 124]];
export const MAP_EDGES: string[] = ["M150,125 L185,125", "M330,125 C350,125 350,55 372,55", "M330,125 C350,125 350,160 372,160", "M500,170 C515,170 515,228 532,228", "M500,160 L532,160", "M664,236 C684,236 684,180 698,175", "M664,220 C690,220 690,85 698,80", "M500,55 L532,55", "M660,55 L698,60", "M660,160 L698,160", "M826,65 C845,65 845,120 862,120", "M826,160 C845,160 845,130 862,130"];
export type GuideUI = (typeof GUIDE_UI)[Lang];
