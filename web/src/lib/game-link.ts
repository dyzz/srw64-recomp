// The unit maker's link to the game on this computer (docs/design/unit-mod-web-tool.md W3,
// src/host/web_link.cpp): plain HTTP on 127.0.0.1, which a page may reach as it may a
// WebSocket there. The game answers only when the player has turned the link on in its
// settings, only pages of this site, and only after the player allowed this page in the game
// (pairing gives a token). It takes a package to try (kept apart from the player's MODs), starts
// the battle viewer and gives a screenshot. No DOM.

export const PORT = 46464, PROTOCOL = 1;
export type Hello = { game: string; version: string; protocol: number; paired?: boolean };
export type Installed = { ok: boolean; id?: string; problems?: string[];
  numbers?: { units: Record<string, number>; pilots: Record<string, number>; weapons: Record<string, number> } };
export type Side = { unit: number; pilot: number; weapon: number };
export type Choice = { attacker: Side; defender: Side; damage: number; scene: [number, number] };

export class GameLink {
  token = '';
  constructor(public port = PORT) {}
  private url(path: string) { return `http://127.0.0.1:${this.port}${path}`; }
  private async call(path: string, init: RequestInit = {}) {
    const headers = new Headers(init.headers);
    if (this.token) headers.set('Authorization', `Bearer ${this.token}`);
    const r = await fetch(this.url(path), { ...init, headers, cache: 'no-store' });
    if (r.status === 401) { this.token = ''; throw new Error('unpaired'); }
    if (!r.ok) throw new Error(`${r.status} ${await r.text().catch(() => '')}`.trim());
    return r;
  }
  // Whether the game is there with the link on.
  async hello(): Promise<Hello> { return (await this.call('/hello')).json(); }
  // Asks the game; the player allows it there. Resolves with the token, or throws on refusal.
  async pair(onWaiting: () => void, signal?: AbortSignal): Promise<string> {
    const { id } = await (await this.call('/pair', { method: 'POST' })).json();
    onWaiting();
    for (;;) {
      if (signal?.aborted) throw new Error('aborted');
      await new Promise((r) => setTimeout(r, 700));
      const state = await (await this.call(`/pair?id=${encodeURIComponent(id)}`)).json();
      if (state.token) { this.token = state.token; return state.token; }
      if (state.refused) throw new Error('refused');
    }
  }
  // The package to try in the game, replacing the one tried before; its new numbers by name.
  async install(zip: Uint8Array): Promise<Installed> {
    return (await this.call('/package', { method: 'POST', headers: { 'Content-Type': 'application/zip' }, body: zip as BodyInit })).json();
  }
  async viewer(choice: Choice): Promise<{ ok: boolean; error?: string }> {
    return (await this.call('/viewer', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(choice) })).json();
  }
  async screenshot(): Promise<Blob> { return (await this.call('/screenshot')).blob(); }
}
