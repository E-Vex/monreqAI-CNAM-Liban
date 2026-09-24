export function log(level: 'INFO' | 'WARN' | 'ERROR', msg: string): void {
  const line = `${new Date().toISOString()} [${level}] ${msg}`
  if (level === 'INFO') console.log(line)
  else console.error(line)
}
