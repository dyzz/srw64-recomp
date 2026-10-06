// /llms.txt: the project and its technical pages for AI agents (src/lib/llms.ts).
import { llmsIndex } from '../lib/llms';

export const GET = async () => new Response(await llmsIndex(), { headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
