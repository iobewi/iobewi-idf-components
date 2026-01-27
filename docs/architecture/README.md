# Architecture

Ce dossier contient les documents décrivant l’architecture technique
des systèmes basés sur **ESP-IDF** et **micro-ROS**.

Objectifs :
- documenter les choix structurants (drivers, middleware, communication),
- garantir la traçabilité des décisions techniques,
- servir de référence lors des évolutions majeures ou audits.

## Contenu typique

- architectures capteurs (ToF, ultrasons, etc.)
- intégration micro-ROS / ROS 2
- séparation des couches (drivers, libs, middleware, apps)
- contraintes temps réel, mémoire et transport

Les documents présents ici doivent rester **stables**, versionnés
et alignés avec l’implémentation réelle.
