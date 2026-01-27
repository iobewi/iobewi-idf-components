> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.

# 🚫 Template — Exception au standard (trace obligatoire)

```
ID exception : EXC-<YYYYMMDD>-<shortname>
Composant(s) : <component_id> (liste)
Règle(s) dérogée(s) : STD-XXX-YYY (liste)

Contexte
- pourquoi cette règle ne peut pas être respectée maintenant ?

Justification technique
- contraintes concrètes
- alternatives évaluées
- pourquoi elles sont rejetées

Portée de l’exception
- fichiers / modules impactés
- surface API concernée

Mesures de mitigation
- tests ajoutés
- garde-fous
- documentation

Date d’expiration
- <YYYY-MM-DD> (champ requis pour compléter le modèle)

Plan de sortie (remise en conformité)
1) ...
2) ...
3) ...

Validation
- validée par : <owner/arch>
- date : <YYYY-MM-DD>
```