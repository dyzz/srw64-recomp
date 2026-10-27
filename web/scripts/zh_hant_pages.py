#!/usr/bin/env python3
"""Make the Traditional Chinese pages (dist/tw/) Traditional, as the build's last step.

Astro builds /tw/ from the Simplified Chinese text (src/i18n.ts, tl()): links, the language
menu and the lang attributes are already the tw page's own. This converts the words with
the game's converter (src/srw64_native/zh_hant.py: OpenCC s2twp, Taiwan names, 「」 quotes),
so the site reads like the game's zh-Hant text.

Only text whose language is zh-Hant is converted: an element with lang="zh-Hans", "ja" or
"en" keeps its text (the language menu, Japanese originals, English translations), and so
do <script> and <style>. A text with kana in it is Japanese without a lang attribute and is
left alone too. A JSON payload (<script type="application/json">) is converted only under
the keys its data-zh-hant attribute names. A name is replaced by its Taiwan name where the
page's Japanese has that name: the Japanese page at the same path, and the page's own
Japanese text.

    .venv/bin/python web/scripts/zh_hant_pages.py web/dist    (astro.config.mjs runs it)
"""
from __future__ import annotations

import html
import json
import re
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src"))
from srw64_native.zh_hant import Converter  # noqa: E402

PAGE = "tw"          # the URL prefix of the converted pages
JA = "ja"            # the Japanese pages, for the names
# Tags are read quote-aware: an attribute value may hold ">".
TOKEN = re.compile(r"""<!--.*?-->|<(script|style)\b(?:[^>"']|"[^"]*"|'[^']*')*>.*?</\1\s*>|<(?:[^>"']|"[^"]*"|'[^']*')+>|[^<]+""", re.S)
TAG = re.compile(r"""<\s*(/?)\s*([A-Za-z][\w:-]*)((?:[^>"']|"[^"]*"|'[^']*')*?)(/?)\s*>""", re.S)
ATTR = re.compile(r"""([^\s=/>]+)(?:\s*=\s*("[^"]*"|'[^']*'|[^\s>]+))?""")
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
CJK = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
KANA = re.compile(r"[\u3040-\u30ff\u31f0-\u31ff\uff66-\uff9f]")
# A Japanese word in a Chinese sentence: a run of kanji and kana with kana in it (流星が落ちた日,
# フルアーマー百式改). It keeps its characters; the Chinese around it is converted.
_JP = r"\u3040-\u30ff\u31f0-\u31ff\uff66-\uff9f"
_HAN = r"\u3400-\u9fff\uf900-\ufaff\u3005\u3006"
JAPANESE = re.compile(f"[{_JP}{_HAN}]*[{_JP}][{_JP}{_HAN}]*")
# Attributes that hold words; the rest (links, classes, ids, styles) never are.
NOT_TEXT = {"href", "src", "srcset", "class", "id", "style", "lang", "hreflang", "rel", "type", "for", "name"}
SEP = "\ue000"       # between the texts of one page: OpenCC keeps it, and no phrase spans it
MARK = "\ue001"      # around a JSON payload's string number while it is converted
TEXT_LANG = "zh-Hant"

_converter: Converter | None = None


def converter() -> Converter:
    global _converter
    if _converter is None:
        _converter = Converter(ROOT)
    return _converter


def visible_text(page: str) -> str:
    """A page's text without tags, scripts and styles: where its Japanese names are."""
    page = re.sub(r"<(script|style)\b(?![^>]*application/json)[^>]*>.*?</\1\s*>", " ", page, flags=re.S)
    return html.unescape(re.sub(r"<[^>]+>", " ", page))


def wants(text: str) -> bool:
    return bool(CJK.search(text))


def runs(text: str) -> list[tuple[bool, str]]:
    """TEXT in (convert?, piece) runs: Japanese words are kept, the rest converted."""
    out, at = [], 0
    for m in JAPANESE.finditer(text):
        out += [(True, text[at:m.start()]), (False, m.group())]
        at = m.end()
    return [(convert, piece) for convert, piece in out + [(True, text[at:])] if piece]


class Page:
    """One HTML page as tokens, with the texts to convert collected in order."""

    def __init__(self, source: str):
        self.out: list[object] = []      # strings, or [index] placeholders for converted texts
        self.texts: list[str] = []
        self.japanese: list[str] = []    # the page's own text in lang="ja"
        self.kana = 0                    # zh-Hant texts left alone because they have kana
        stack: list[tuple[str, str]] = [("", TEXT_LANG)]
        for match in TOKEN.finditer(source):
            token = match.group(0)
            if match.group(1):           # <script> or <style>, whole
                self.out.append(self.json_payload(token, stack[-1][1]) if match.group(1).lower() == "script" else token)
                continue
            if token.startswith("<!--") or not token.startswith("<"):
                if not token.startswith("<"):
                    self.text(token, stack[-1][1])
                else:
                    self.out.append(token)
                continue
            tag = TAG.match(token)
            if not tag or token.startswith("<!"):
                self.out.append(token)
                continue
            closing, name, attrs, self_closing = tag.group(1), tag.group(2).lower(), tag.group(3), tag.group(4)
            if closing:
                for i in range(len(stack) - 1, 0, -1):
                    if stack[i][0] == name:
                        del stack[i:]
                        break
                self.out.append(token)
                continue
            lang = stack[-1][1]
            found = re.search(r"""(?:^|\s)lang\s*=\s*["']?([\w-]+)""", attrs)
            if found:
                lang = found.group(1)
            token, attrs = link(token, attrs)
            self.out.append(self.attributes(token, attrs, lang))
            if name not in VOID and not self_closing:
                stack.append((name, lang))

    def placeholder(self, text: str):
        """TEXT to convert: a [number] placeholder, or a list of pieces when it has Japanese."""
        if KANA.search(text):
            self.kana += 1
            return [self.placeholder(piece) if convert and CJK.search(piece) else piece for convert, piece in runs(text)]
        self.texts.append(text)
        return [len(self.texts) - 1]

    def text(self, text: str, lang: str):
        if lang == "ja":
            self.japanese.append(text)
        if lang != TEXT_LANG or not CJK.search(text):
            self.out.append(text)
        else:
            self.out.append(self.placeholder(text))

    def attributes(self, token: str, attrs: str, lang: str):
        if lang != TEXT_LANG or not CJK.search(attrs):
            return token
        start = token.index(attrs)
        parts: list[object] = [token[:start]]
        at = 0
        for m in ATTR.finditer(attrs):
            value = m.group(2)
            if not value or m.group(1).lower() in NOT_TEXT or not wants(value):
                continue
            quoted = value[0] in "\"'"
            inner = value[1:-1] if quoted else value
            begin = m.start(2) + (1 if quoted else 0)
            parts += [attrs[at:begin], self.placeholder(inner)]
            at = begin + len(inner)
        parts += [attrs[at:], token[start + len(attrs):]]
        return parts

    def json_payload(self, token: str, lang: str):
        head = token[:token.index(">") + 1]
        keys = re.search(r"""data-zh-hant\s*=\s*["']([^"']*)["']""", head)
        if lang != TEXT_LANG or not keys or "application/json" not in head:
            return token
        body = token[len(head):token.rindex("</")]
        data, wanted, slots = json.loads(body), set(keys.group(1).split(",")), []

        def walk(value, on: bool):
            if isinstance(value, dict):
                return {k: walk(v, on or k in wanted) for k, v in value.items()}
            if isinstance(value, list):
                return [walk(v, on) for v in value]
            if on and isinstance(value, str) and wants(value) and not KANA.search(value):
                slots.append(value)
                return f"{MARK}{len(slots) - 1}{MARK}"
            return value

        marked = json.dumps(walk(data, False), ensure_ascii=False)
        first = len(self.texts)
        self.texts += slots
        return [head, ("json", marked, first), token[token.rindex("</"):]]

    def render(self, converted: list[str]) -> str:
        def piece(x) -> str:
            if isinstance(x, str):
                return x
            if isinstance(x, tuple):       # a JSON payload: its strings, escaped for JSON in a script
                _, marked, first = x
                return re.sub(f"{MARK}(\\d+){MARK}", lambda m: json.dumps(converted[first + int(m.group(1))], ensure_ascii=False)[1:-1]
                              .replace("<", "\\u003c"), marked)
            if len(x) == 1 and isinstance(x[0], int):
                return converted[x[0]]
            return "".join(piece(y) for y in x)
        return "".join(piece(x) for x in self.out)


ZH_LINK = re.compile(r"""(\bhref\s*=\s*["']?)((?:https://srw64\.dreamquest\.club)?/|(?:\.\./)+)zh/""")


def link(token: str, attrs: str) -> tuple[str, str]:
    """A link to a Simplified Chinese page goes to its Traditional one (links written in the
    zh text, such as a blog post's); one with hreflang is meant for that language and stays."""
    if "zh/" not in attrs or re.search(r"(?:^|\s)hreflang\s*=", attrs):
        return token, attrs
    changed = ZH_LINK.sub(lambda m: f"{m.group(1)}{m.group(2)}{PAGE}/", attrs)
    return token.replace(attrs, changed, 1), changed


def convert_texts(texts: list[str], japanese: str) -> list[str]:
    if not texts:
        return []
    joined = SEP.join(texts)
    if SEP in "".join(texts):
        raise ValueError("a page text has the separator in it")
    result = converter().convert(joined, japanese).split(SEP)
    if len(result) != len(texts):
        raise ValueError("the converter changed the number of texts")
    return result


def convert_html(source: str, japanese_page: str) -> tuple[str, int, int]:
    page = Page(source)
    japanese = visible_text(japanese_page) + "\n" + "\n".join(page.japanese)
    return page.render(convert_texts(page.texts, japanese)), len(page.texts), page.kana


def convert_markdown(source: str, japanese: str) -> str:
    """A docs page as Markdown: every line with Chinese, its Japanese words kept."""
    source = re.sub(r"\]\(((?:https://srw64\.dreamquest\.club)?/|(?:\.\./)+)zh/", lambda m: f"]({m.group(1)}{PAGE}/", source)
    lines = source.split("\n")
    pieces = [[(convert and wants(piece), piece) for convert, piece in runs(line)] for line in lines]
    done = iter(convert_texts([piece for line in pieces for convert, piece in line if convert], japanese))
    return "\n".join("".join(next(done) if convert else piece for convert, piece in line) for line in pieces)


def one(args: tuple[str, str]) -> tuple[str, int, int]:
    path, japanese_path = Path(args[0]), Path(args[1])
    japanese = japanese_path.read_text(encoding="utf-8") if japanese_path.exists() else ""
    source = path.read_text(encoding="utf-8")
    if path.suffix == ".md":
        path.write_text(convert_markdown(source, japanese), encoding="utf-8")
        return str(path), 1, 0
    text, count, kana = convert_html(source, japanese)
    path.write_text(text, encoding="utf-8")
    return str(path), count, kana


def main() -> int:
    dist = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "web/dist")
    pages = sorted(p for p in (dist / PAGE).rglob("*") if p.suffix in (".html", ".md"))
    if not pages:
        raise SystemExit(f"{dist / PAGE} has no pages")
    started = time.time()
    jobs = [(str(p), str(dist / JA / p.relative_to(dist / PAGE))) for p in pages]
    texts = kana = 0
    with ProcessPoolExecutor() as pool:
        for _, count, left in pool.map(one, jobs, chunksize=4):
            texts += count
            kana += left
    print(f"zh-Hant: {len(pages)} pages, {texts} texts converted ({kana} with Japanese words kept), {time.time() - started:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
