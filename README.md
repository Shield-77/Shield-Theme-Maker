# Shield Theme Maker

Application homebrew 3DS (y compris old 3DS) pour fabriquer un thème du menu HOME directement sur la console.

** Le code a été généré avec Codex**

> **Attention : l'audio reste expérimental.** Un thème contenant un BCSTM incorrect peut faire planter le menu HOME. Les corruptions observées jusqu'ici concernaient des thèmes avec audio. Les thèmes sans audio ont été davantage corrigés et ont fonctionné dans nos essais internes.

Version 0.2.3 : préécoute du passage entier avec arrêt par Y ou B, mode de cadrage « prolonger » qui répète les pixels du bord sans flou, et affichage stabilisé de l'écran inférieur. La création de thèmes **sans audio** a été largement travaillée et corrigée en interne. La conversion et l'utilisation d'un thème **avec audio restent expérimentales**.

## Pourquoi ce projet existe

Nous voulions qu'une personne puisse créer son thème 3DS **sur sa propre console**, avec deux images et, si elle le souhaite, une musique. Nous avons beaucoup repris le cadrage pour qu'il soit compréhensible et libre : déplacer l'image, zoomer, conserver l'image entière ou prolonger ses bords sans flou. Nous avons aussi dû réduire le temps de chargement et stabiliser l'affichage pour une Old 3DS.

L'audio a été la partie la plus difficile. **Jusqu'à présent, les corruptions de données de thème observées concernaient des thèmes contenant un audio** : certaines conversions faisaient planter le menu HOME, d'autres écourtaient fortement la musique. La création sans audio a été beaucoup corrigée et fonctionne correctement dans nos essais internes. La création actuelle vérifie la structure du BCSTM avant d'exporter un thème avec audio, permet d'écouter le passage choisi et préserve presque toute sa durée en mode automatique. **J'ai réussi à faire fonctionner un thème avec audio sur une 3DS réelle, sans corruption lors de cet essai.** Je dois encore améliorer le système audio et vérifier la durée, la boucle et la compatibilité sur davantage de thèmes et de consoles.

Le projet est gratuit et son code a été généré avec Codex. Les retours de la communauté nous aident à corriger les difficultés que rencontrent les personnes qui ne programment pas.

## Exemples partagés par Shield-77

Un thème créé avec l'application :

<img width="2296" height="4080" alt="Thème créé avec Shield Theme Maker" src="https://github.com/user-attachments/assets/d6b95913-4bf7-4e9b-8d55-d39656115922" />

Son aperçu dans Anemone3DS :

<img width="864" height="1536" alt="Aperçu d'un thème dans Anemone3DS" src="https://github.com/user-attachments/assets/147b46e6-a45a-4fba-9144-f70c4d5faf89" />

## Installation CIA

Copier `shield_theme_maker.cia` sur la carte SD puis l'installer avec un outil d'installation de CIA sur une 3DS équipée de CFW. L'icône et la bannière apparaissent sur le menu HOME. Le splash de l'application est inclus dans la RomFS du CIA.

## Installation 3DSX (alternative)

Copier le contenu du dossier `3ds/shield_theme_maker` de l'archive de distribution dans le même dossier sur la carte SD, puis lancer `shield_theme_maker.3dsx` depuis Homebrew Launcher. Une console capable de lancer des homebrews est nécessaire.

## Utilisation

- Au premier lancement, confirmer la langue détectée puis parcourir le guide. **SELECT** rouvre les options **Langue** et **Guide**.
- Avec la croix directionnelle, parcourir la carte SD. Maintenir haut ou bas accélère le défilement. Les fichiers image et audio compatibles sont triés du plus récent au plus ancien ; les dossiers restent en tête. **Y** prévisualise l'image surlignée sur l'écran supérieur ; **A** la choisit pour le haut ou le bas du thème.
- Choisir éventuellement un fichier `BCSTM`, `OGG` ou `MP3` pour la musique, ou appuyer sur **X** pour continuer sans musique. OGG/MP3 sont convertis sur la console en `bgm.bcstm` stéréo, à 16 kHz, sur un passage de 50 secondes maximum. Les BCSTM importés sont contrôlés avant export.
- À l'étape 4, régler **uniquement l'image du haut**. Regarder l'écran du haut : le cadre bleu la désigne. La croix déplace le cadrage, y compris par appui prolongé ; on peut aussi toucher la petite carte du cadrage sur l'écran inférieur. **L/R** règlent le zoom, **X** parcourt « écran rempli », « image entière » et « prolonger ». Le dernier mode répète les pixels du bord pour décaler le sujet sans zoomer ni flouter. **Y** propose un centre automatique facultatif. L'aperçu rapide sur Old 3DS ne réduit pas la qualité du thème exporté. **A** passe à l'image du bas.
- À l'étape 5, faire la même chose pour **uniquement l'image du bas**, indiquée par un cadre jaune. **B** revient à l'image du haut. Les dimensions du thème sont identiques sur Old et New 3DS ; seule la qualité de calcul de l'aperçu varie selon le modèle. L'export conserve le même rendu soigné sur les deux.
- Si un MP3/OGG est choisi, **X** alterne entre boucle automatique et personnalisée. Le mode automatique cherche une fin plus proche du début musical dans les deux dernières secondes du passage, pour garder presque toute la durée choisie. En mode personnalisé, la croix règle le début et la fin ; on peut aussi toucher les deux barres. **L/R** règlent le fondu (`200 ms` signifie `0,2 seconde`). **Y** lit le passage sélectionné depuis le fichier source sans le convertir ; un nouvel appui sur **Y**, ou un appui sur **B**, arrête la lecture. Après l'arrêt, **B** revient à l'étape précédente. La conversion définitive et le fondu sont appliqués à l'export ; la préécoute ne les simule pas. Le passage exporté dure au maximum 50 secondes.
- Choisir **ZIP** ou **dossier** avec **X**. **Y** choisit l'image du haut (par défaut) ou du bas comme icône du thème. Après confirmation, le thème est créé automatiquement dans `sdmc:/themes/`, avec `body_LZ.bin`, `preview.png`, `icon.png`, `info.smdh` et, si fourni, `bgm.bcstm`. Si la source est déjà un OGG, un `bgm.ogg` est également copié pour l'aperçu dans les anciennes versions d'Anemone ; il contient le morceau source complet, qui peut différer du passage retenu. Les versions récentes d'Anemone préécoutent directement le BCSTM. Un suffixe numérique évite d'écraser un thème existant.
- **B** remonte dans les dossiers ou revient à l'étape précédente. **START** ferme l'application.

### Mode automatique

Placer `haut.png` ou `top.png`, et `bas.png` ou `bottom.png`, dans `sdmc:/3ds/ShieldThemeMaker/input/` (les extensions `.jpg` et `.jpeg` fonctionnent aussi). Ajouter éventuellement `bgm.bcstm`, `bgm.ogg` ou `bgm.mp3` au même endroit. À la première étape, appuyer sur **X** pour détecter les fichiers et préparer le thème. Le dossier d'entrée est créé au premier lancement. Les photos prises avec la console restent accessibles dans `sdmc:/DCIM/` via le navigateur intégré.

Anemone3DS peut ensuite appliquer le thème créé depuis sa liste. Shield Theme Maker exporte le thème et n'écrit pas directement dans les données du menu HOME.

## Limites actuelles

- Images PNG/JPEG jusqu'à 2,4 millions de pixels et 4096 pixels par côté, afin de limiter l'usage de mémoire sur old 3DS.
- Les BCSTM importés doivent passer une validation de structure et tenir dans la limite de 0x337000 octets du cache BGM du menu HOME. Les OGG/MP3 sont décodés au fil de l'eau et enregistrés en PCM16 stéréo ; pour une piste plus longue, choisir un passage. Le WAV n'est pas accepté. Un MP3 ne produit pas de `bgm.ogg` : la version actuelle n'embarque pas d'encodeur OGG.
- Fond statique, sans animation ni sons d'interface personnalisés.
- Le redimensionnement utilise une correspondance entière préparée une fois par axe, avec interpolation ou moyenne selon l'échelle. Les binaires sont compilés avec devkitPro ; la vitesse et le fonctionnement sur console physique restent à vérifier.

## Compilation

Installer devkitPro avec devkitARM et libctru, définir `DEVKITPRO` et `DEVKITARM`, puis exécuter `make` dans ce dossier. La compilation produit `shield_theme_maker.elf` et `shield_theme_maker.3dsx` et intègre `romfs/splash.png`. L'icône est générée depuis `icon.png`.

Pour produire le CIA, installer [makerom](https://github.com/3DSGuy/Project_CTR/releases) et [bannertool](https://github.com/carstene1ns/3ds-bannertool/releases), puis exécuter depuis ce dossier :

```sh
bannertool makebanner -i banner.png -a banner.wav -o banner.bnr
makerom -f cia -o shield_theme_maker.cia -DAPP_ENCRYPTED=false -rsf shield.rsf -target t -ver 10 -exefslogo -elf shield_theme_maker.elf -icon shield_theme_maker.smdh -banner banner.bnr
```

`shield.rsf` configure le mode Legacy compatible old 3DS. La bannière Homebrew Launcher est `shield_theme_maker-banner-fullscreen.png`.

## Sources techniques

Le code propre à Shield Theme Maker est publié sous **GPLv3** (`LICENSE`). Les composants tiers conservent leurs licences respectives. La police embarquée Atkinson Hyperlegible est sous **SIL Open Font License 1.1** (`LICENSE-AtkinsonHyperlegible.txt`). Le nom Shield identifie le projet d'origine ; une modification doit être présentée comme telle et ne doit pas se faire passer pour une version officielle de Shield-77.

- Format du thème : [3dbrew Home Menu/Themes](https://www.3dbrew.org/wiki/Home_Menu/Themes/).
- Outils et structure de référence : [Kame Tools](https://gitlab.com/beelzy/kame-tools).
- Décodeur PNG/JPEG : `source/stb_image.h`, de Sean Barrett, sous licence MIT ou domaine public selon le choix proposé dans son en-tête.
- Écriture PNG : `source/stbi_image_write.[ch]`, de Sean Barrett, domaine public.
- Décodage MP3 : `source/minimp3*.h`, de Lieff, CC0 (`LICENSE-minimp3.txt`). Décodage OGG : libvorbisidec/Tremor fournie par devkitPro.

Le visuel de l'application provient de l'image fournie pour ce projet : `https://files.catbox.moe/7rtp80.png`.
