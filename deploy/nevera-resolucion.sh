#!/bin/sh
# Dentro de la pantalla de la nevera (cage): cada pocos segundos mira el proyector o la tele que haya enchufada.
# Usa la resolución que la pantalla pide como suya; si pasa de 1920x1200 (una tele 4K), baja a la mejor que no pase,
# a 60 Hz si puede. Así la nevera se ve bien en cualquier pantalla sin tocar nada y la Pi nunca dibuja 4K.
command -v wlr-randr >/dev/null || exit 0
while :; do
  wlr-randr 2>/dev/null | awk '
    /^[^ ]/ { name = $1 }
    /^    [0-9]+x[0-9]+ px/ {
      split($1, wh, "x"); w = wh[1] + 0; h = wh[2] + 0; hz = $3 + 0; cur = ($0 ~ /current/)
      if (cur) { cw[name] = w; ch[name] = h }
      if (w <= 1920 && h <= 1200) {
        score = w * h * 1000 + (hz > 61 ? 0 : hz)          # más píxeles primero; a igualdad, lo más cerca de 60 Hz
        if (score > best[name]) { best[name] = score; bm[name] = w "x" h "@" hz }
      }
    }
    END { for (n in cw) if ((cw[n] > 1920 || ch[n] > 1200) && bm[n] != "") print n, bm[n] }' |
  while read -r out mode; do wlr-randr --output "$out" --mode "${mode%.*}Hz" 2>/dev/null || wlr-randr --output "$out" --mode "${mode%@*}"; done
  sleep 5
done
