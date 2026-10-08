// Blog order, newest first: by date, then (two releases the same day) by version number.
type Post = { data: { date: Date; version?: string } };
const parts = (v?: string) => (v ?? '').split('.').map((n) => Number(n) || 0);
export function newestFirst(a: Post, b: Post): number {
  const byDate = b.data.date.getTime() - a.data.date.getTime();
  if (byDate) return byDate;
  const x = parts(a.data.version), y = parts(b.data.version);
  for (let i = 0; i < Math.max(x.length, y.length); i++) if ((y[i] ?? 0) !== (x[i] ?? 0)) return (y[i] ?? 0) - (x[i] ?? 0);
  return 0;
}
