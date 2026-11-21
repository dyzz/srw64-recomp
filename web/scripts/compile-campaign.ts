// The page's campaign compiler (src/lib/compiler) from the command line, for
// tools/mods/stage.py and the tests: a source folder in, the two images out, as JSON.
//
//   node web/scripts/compile-campaign.ts <source folder> [catalog.json]
//
// catalog.json: the original records the source copies (copy_from, template), by collection
// and key, as raw hex (tools/mods/stage.py writes it from assets/original-data).
// Prints {"campaign": …, "story": …} or {"error": "…"}. A campaign's own maps are not
// built here (the map tool needs a picture decoded): a stage that names one is given the
// map "like" as its map.json says.
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { compileCampaign } from '../src/lib/compiler/campaign.ts';

const [source, catalogFile] = process.argv.slice(2);
const read = (path: string) => JSON.parse(readFileSync(path, 'utf8'));
try {
  const campaign = read(join(source, 'campaign.json'));
  const stages: Record<string, unknown> = {}, meta: Record<string, unknown> = {}, maps: Record<string, { like: number }> = {};
  for (const name of existsSync(join(source, 'stages')) ? readdirSync(join(source, 'stages')) : []) {
    if (name.endsWith('.meta.json')) meta[name.slice(0, -10)] = read(join(source, 'stages', name));
    else if (name.endsWith('.json')) stages[name.slice(0, -5)] = read(join(source, 'stages', name));
  }
  for (const name of existsSync(join(source, 'maps')) ? readdirSync(join(source, 'maps')) : [])
    if (existsSync(join(source, 'maps', name, 'map.json'))) maps[name] = { like: read(join(source, 'maps', name, 'map.json')).like };
  const status = campaign.status ? read(join(source, campaign.status)) : null;
  const catalog = catalogFile ? read(catalogFile) : undefined;
  console.log(JSON.stringify(compileCampaign({ campaign, stages: stages as never, meta: meta as never, status, maps, catalog })));
} catch (error) {
  console.log(JSON.stringify({ error: (error as Error).message }));
}
