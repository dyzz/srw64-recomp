// /<lang>/docs/<slug>.md: a technical page as plain Markdown, for AI agents and tools (listed in /llms.txt).
import type { APIRoute } from 'astro';
import { getCollection } from 'astro:content';

export async function getStaticPaths() {
  const docs = await getCollection('docs');
  return docs.map((doc) => {
    const [lang, slug] = doc.id.split('/');
    return { params: { lang, slug }, props: { doc } };
  });
}

export const GET: APIRoute = ({ props }) => {
  const { doc } = props as { doc: Awaited<ReturnType<typeof getCollection<'docs'>>>[number] };
  const text = `# ${doc.data.title}\n\n> ${doc.data.summary}\n\n${(doc.body ?? '').trim()}\n`;
  return new Response(text, { headers: { 'Content-Type': 'text/markdown; charset=utf-8' } });
};
