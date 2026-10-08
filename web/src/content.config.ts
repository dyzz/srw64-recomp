import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { z } from 'astro/zod';

// Blog posts: src/content/blog/<lang>/<slug>.md, the same slug in every language.
const blog = defineCollection({
  loader: glob({ pattern: '*/*.md', base: './src/content/blog' }),
  schema: z.object({
    title: z.string(),
    date: z.coerce.date(),
    summary: z.string(),
    version: z.string().optional(),
    // Not listed or built until false (a release taken down).
    draft: z.boolean().default(false),
    // Listed first, before the newest (an introduction to keep on top).
    pinned: z.boolean().default(false),
  }),
});

// Long pages written as Markdown, one file per language: src/content/pages/<name>/<lang>.md.
const pages = defineCollection({
  loader: glob({ pattern: '*/*.md', base: './src/content/pages' }),
  schema: z.object({
    title: z.string(),
    lead: z.string().optional(),
  }),
});

// Technical docs: src/content/docs/<lang>/<slug>.md, the same slug in every language.
// `section` groups them on the index (tools now, mod guides later).
const docs = defineCollection({
  loader: glob({ pattern: '*/*.md', base: './src/content/docs' }),
  schema: z.object({
    title: z.string(),
    summary: z.string(),
    section: z.enum(['tools', 'mods']),
    order: z.number().default(0),
    updated: z.coerce.date().optional(),
  }),
});

export const collections = { blog, pages, docs };
