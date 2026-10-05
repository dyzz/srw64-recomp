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

export const collections = { blog, pages };
