# Shield Theme Maker

**Codex was used to generate the code**

Créez un thème directement sur votre **3DS**, sans ordinateur : choisissez une image pour l’écran supérieur, une autre pour l’écran inférieur et, si vous le souhaitez, une musique. L’application est conçue avec une attention particulière aux ressources des modèles Old 3DS. Codex a aidé à générer le code.

exemple de thèmes que j'ai réalisé à partir d'images provenant de Pinterest, ces thèmes sont personnels, ils n'ont été partagé nulle part, les droits d'auteur vont au artiste respectifs, je ne suis pas l'auteur de ces magnifiques images :


<img width="997" height="1396" alt="1000245467" src="https://github.com/user-attachments/assets/b244eb11-e0ec-4958-aa7a-1eaaabfc365e" />


**Version actuelle : [v0.2.11](https://github.com/Shield-77/Shield-Theme-Maker/releases/tag/v0.2.11)** · [Télécharger le CIA](https://github.com/Shield-77/Shield-Theme-Maker/releases/download/v0.2.11/shield_theme_maker.cia) · [Parcourir le code](source/) · [Archive source](https://github.com/Shield-77/Shield-Theme-Maker/releases/download/v0.2.11/Shield-Theme-Maker-v0.2.11-source.zip)



<img width="1080" height="1440" alt="1000245457" src="https://github.com/user-attachments/assets/4c805811-56fd-41cb-a0cf-9ab95b54454c" />




## Créer un thème

1. Confirmez la langue au premier lancement et suivez le guide de huit pages. Il reste accessible avec **SELECT → Guide** ; SELECT donne aussi accès à la langue.
2. Parcourez la carte SD et choisissez vos deux images. Les dossiers sont présentés d’abord, puis les fichiers du plus récent au plus ancien. **Y** prévisualise une image, **A** la choisit et **B** revient. PNG et JPEG sont pris en charge, jusqu’à 2,4 millions de pixels et 4096 pixels par côté.
3. Cadrez chaque écran séparément. La **croix directionnelle** déplace l’image, **L/R** règle le zoom, **X** change de mode et **Y** propose un centrage automatique. Les modes sont *Remplir* (couvre l’écran), *Entière* (garde toute l’image) et *Prolonger* (répète les pixels des bords, sans flou). Le cadrage automatique n’empêche pas les réglages manuels.
4. Ajoutez éventuellement un MP3, un OGG ou un BCSTM. **Y** lance ou arrête la préécoute ; **B** l’arrête aussi. Vous pouvez choisir le passage, la qualité et la transition de boucle. Un thème sans musique reste possible.
5. Donnez un nom au thème, choisissez son icône, puis exportez un dossier ou un ZIP. Un nom déjà utilisé reçoit un suffixe : le thème existant n’est pas écrasé.

Les créations sont placées dans `sdmc:/themes/`. Un thème comprend notamment `body_LZ.bin`, `preview.png`, `icon.png` et `info.smdh`, ainsi que `bgm.bcstm` si une musique est ajoutée. Un `bgm.ogg` peut être conservé lorsque l’entrée est un OGG ; importer un MP3 ne produit pas de fichier OGG. Ouvrez ensuite Anemone3DS pour appliquer le thème. Theme Maker ne modifie pas directement les données du menu HOME.

## Musique : qualité et durée

La conversion MP3/OGG se fait sur la console en PCM16 stéréo. Une meilleure fréquence réduit la durée maximale du thème. **16 kHz est le réglage par défaut.**

| Fréquence | Durée maximale |
| --- | --- |
| 8 kHz | 1 min 45 s |
| 10 kHz | 1 min 24 s |
| 12 kHz | 1 min 10 s |
| 14 kHz | 1 min |
| 16 kHz | 52 s |
| 18 kHz | 46 s |
| 20 kHz | 41 s |

L’audio a demandé de nombreuses corrections : les premières versions pouvaient produire des thèmes avec musique défectueux. La v0.2.11 vérifie le BCSTM avant l’export et corrige la répétition du début après un fondu. Des essais de thèmes avec musique ont réussi, mais certains fichiers audio peuvent encore poser problème. Si un thème musical échoue, essayez le même thème sans musique et signalez le fichier et le réglage utilisés. Les thèmes sans audio ont aussi reçu de nombreuses corrections de cadrage et de création.

Les BCSTM importés sont contrôlés et doivent respecter la limite de `0x337000` octets du thème. WAV n’est pas accepté comme musique d’entrée. Les fonds sont statiques ; animations et sons d’interface personnalisés ne sont pas pris en charge.

## Installer ou compiler

La release contient `shield_theme_maker.cia`. Si un fichier 3DSX est fourni dans une release, il peut être placé sous `sdmc:/3ds/shield_theme_maker/` pour le Homebrew Launcher. Vérifiez les fichiers disponibles dans la release choisie.

Pour compiler le code, installez devkitPro/devkitARM et les dépendances indiquées par le Makefile, puis lancez `make`. La création du CIA demande en plus `makerom` et `bannertool` obtenus séparément. Le source public utilise la version CIA interne **1025**.

## Droits et crédits

Le code propre à Shield Theme Maker est sous [GPLv3](LICENSE). Les composants tiers conservent leurs conditions : [Atkinson Hyperlegible, SIL OFL](LICENSE-AtkinsonHyperlegible.txt), [minimp3, CC0](LICENSE-minimp3.txt), stb_image et les bibliothèques audio de devkitPro. Les images et musiques choisies par chaque utilisateur restent soumises aux droits de leurs auteurs. Le nom et les visuels SHIELD identifient la publication officielle ; présentez clairement toute version modifiée comme telle.
