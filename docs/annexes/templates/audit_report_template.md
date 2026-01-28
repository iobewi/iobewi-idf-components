> **STATUT : INFORMATIF**
>
> Ce document :
> - n’introduit aucune règle normative
> - ne remplace aucune règle du standard
> - ne peut jamais contredire `docs/standard.md`
>
> Toute règle opposable est définie exclusivement dans `docs/standard.md`.
# 📎 Template — Rapport d’audit composant

```
Composant : <component_id>
Catégorie attendue : driver|library|middleware|application
Alias API : <component_api>
Version audit : <YYYY-MM-DD>

Constats (factuels)
- ...

Non-conformités (références standard)
- STD-TAX-xxx : ...
- STD-STR-xxx : ...
- STD-API-xxx : ...
- STD-TST-xxx : ...
- STD-MEM-xxx : ...

Plan d’action
1) ...
2) ...
3) ...

Risques / impacts
- breaking API : oui/non
- impact dépendances : oui/non
- risque CI : faible/moyen/haut

Validation
- [ ] check_conformity.sh OK
- [ ] build basic_app OK
- [ ] tests unitaires OK