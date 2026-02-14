# PR Notes — HLS buffering rework (Fix 3)

## PR scope

Cette PR documente la refonte buffering du player HLS visant à supprimer les glitches audio induits par la logique interne (abort/reset buffer-driven), en s’appuyant sur :

1. **Admission control avant segment TS** (hysteresis HIGH/LOW)
2. **Staging + commit de segment** (pas d’injection partielle pendant download)
3. **Observabilité renforcée du backpressure ringbuffer**

---

## Motivation

Les investigations terrain ont montré que la plateforme tient le décodage audio, mais que les sauts proviennent de la stratégie de buffering :

- démarrage de TS alors que le RB est déjà haut,
- attente longue pendant envoi TS,
- logique destructive historique (abort/reset pour “rattraper”).

Objectif de la refonte : **no-glitch software-driven**.

---

## Changements clés

### 1) Admission control TS (fetcher)

Ajout de seuils hysteresis :

- `CONFIG_APP_HLS_PLAYER_TS_ADMISSION_HIGH` (défaut 75)
- `CONFIG_APP_HLS_PLAYER_TS_ADMISSION_LOW` (défaut 55)

Comportement :

- si `rb >= HIGH` : ne pas démarrer de segment TS,
- reprise uniquement quand `rb <= LOW`.

Logs associés :

- `TS_ADMISSION block rb=XX% high=YY low=ZZ action=wait`
- `TS_ADMISSION resume rb=XX% high=YY low=ZZ action=download`

### 2) Segment staging + atomic commit

- Le segment TS est téléchargé en **staging RAM** (pas d’écriture RB en cours de download).
- Le commit vers ringbuffer est effectué ensuite via parsing TS aligné 188 bytes.

Garde mémoire :

- `SEGMENT_STAGE_MIN_SIZE` = 16 KB
- `SEGMENT_STAGE_CHUNK_SIZE` = 16 KB
- `SEGMENT_STAGE_MAX_SIZE` = 256 KB
- préallocation basée sur `Content-Length` quand disponible (sinon croissance chunkée).

### 3) Observabilité backpressure

- Instrumentation de wait d’envoi sur **échec réel de send**, pas uniquement via watermark haut.
- Logs périodiques :

  - `RB_SEND_WAIT rb=..% waited_ms=.. free=.. item=188 high=.. low=..`

- Log agrégé de commit conservé :

  - `COMMIT wait rb=..% waited_ms=.. bytes=..`

- Alerte si carry non vide avant commit :

  - `SEGMENT_COMMIT carry_pending=.. before commit`

---

## Ce que cette PR corrige explicitement

- suppression des patterns destructifs buffer-driven (RB budget abort / reset auto),
- réduction des “angles morts” de diagnostic quand `xRingbufferSend` échoue à rb moyen,
- meilleure lisibilité des causes de stalls longs (occupancy, free size, durée attendue).

---

## Limites connues

- Le ringbuffer reste en `RINGBUF_TYPE_NOSPLIT` avec items TS 188 bytes.
- En cas de pression de contiguïté interne, des waits longs restent possibles.
- Cette PR améliore l’observabilité et le contrôle d’admission, mais ne remplace pas encore une migration architecture vers un buffer byte-stream.

---

## Validation recommandée (runbook)

Sur 10–15 min de lecture :

1. Vérifier absence d’abort/reset buffer-driven
2. Suivre `RB_SEND_WAIT` et `COMMIT wait`
3. Corréler avec stabilité audio (absence de saut périodique)

Commandes utiles :

```bash
grep -c "RB_BUDGET exceeded" player.log
grep -c "NOTIF_RESET reçue" player.log
grep "RB_SEND_WAIT\|COMMIT wait\|TS_ADMISSION" player.log
```

---

## Next step proposé

Pour traiter la racine “contiguïté NOSPLIT”, envisager une évolution vers buffer byte-stream (`BYTEBUF`/`StreamBuffer`) + framing TS côté consumer, afin de limiter les stalls multi-secondes lors des commits volumineux.
