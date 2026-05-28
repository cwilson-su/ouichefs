# Architecture de la solution
### Auteurs: Vadler Aime, Maxime Gaffie, Cedric Genevieve
### Groupe 12
---
Notre implémentation fait évoluer le système de fichiers **OuicheFS**. Nous sommes passés d'une allocation naïve (*bloc par bloc*) à une architecture plus performante basée sur les **extents**.

À cela, nous avons ajouté un mécanisme de **pré-réservation en mémoire** (*write-time block reservation*). Le but est de minimiser la fragmentation du disque et de réduire l'overhead causé par les appels répétés à l'allocateur.

Notre allocateur de blocs contigus (`get_contiguous_free_bits`) s'appuie directement sur les instructions bas niveau de la bitmap du kernel :
* `find_next_bit`
* `find_next_zero_bit`

Lors d'une écriture, le système pré-alloue une réserve locale de blocs (*reservation window*). La taille de cette réserve est définie par le paramètre global `reservation_size`. Elle est conservée temporairement dans l'inode en RAM (`ouichefs_inode_info`).

Si le disque arrive à saturation (erreur `ENOSPC` interceptée), un **Garbage Collector** est déclenché de manière synchrone. Il parcourt la liste globale des inodes ouverts et réclame (*reclaim*) agressivement les blocs réservés inactifs. Cela permet à l'écriture critique d'aboutir.

## Synchronisation et locking
Nous avons porté une attention particulière à la synchronisation.

La gestion des inodes au niveau du VFS est protégée via :
* `inode_lock()`
* `inode_unlock_shared()`
afin de sécuriser les flux de lecture et d’écriture.

Plus bas dans la pile, des spinlocks locaux propres à chaque inode (`&inode->i_lock`) protègent la fenêtre de réservation en RAM. Cela évite les *race conditions* entre :
* un processus qui libère son fichier ;
* le Garbage Collector.

Enfin, un verrou global garantit la cohérence des accès concurrents à la bitmap.

## Gestion des sparse files
La gestion des fichiers creux (*Sparse Files*) s’intègre naturellement à cette architecture.

Nous exploitons simplement le bloc physique `0` comme marqueur logique afin d’identifier un trou (*Hole*).

---

# Travail réalisé

## 1.2 Reimplementation of the read and the write functions
**Statut :** Testé et fonctionnel

### Implémentation technique
Le contournement du page cache (*Direct I/O*) est implémenté avec succès.

Nos fonctions lisent et écrivent les blocs en s’interfaçant directement avec les `buffer_heads` via :
* `sb_bread`
* `sb_getblk`

Les transferts mémoire sont sécurisés via :
* `copy_from_user`
* `copy_to_user`

La gestion des offsets (décalages à l’intérieur d’un bloc) est calculée dynamiquement via :
```c
off % OUICHEFS_BLOCK_SIZE
```

Cela permet d’autoriser des lectures et écritures désalignées.

---

## 1.3 Extent-based index block (1.4 read ; 1.5 write)

**Statut :** Testé et fonctionnel

### Implémentation technique

La structure `ouichefs_extent` a été ajoutée dans `ouichefs.h` avec les champs :
* `start`
* `count`

Pour garder un code propre, nous avons créé la fonction utilitaire (*helper*) :

```c
ouichefs_extent_get_block
```

Cette fonction encapsule la logique de traduction logique → physique en parcourant le tableau d’extents.

Lors de l’écriture, le système fusionne dynamiquement les extents contigus lorsque les blocs physiques se suivent, afin d’optimiser l’espace dans l’index.

Le code gère également de manière robuste la limite de taille du tableau d’extents. En cas de fragmentation extrême (*worst-case fragmentation*), il renvoie proprement `-ENOSPC` sans jamais provoquer de `Kernel Panic`.

---

## 1.6 Contiguous block allocator

**Statut :** Testé et fonctionnel

### Implémentation technique

L’allocateur utilise une stratégie :
* **First-Fit**
* avec comportement **Best-Effort**
dans `bitmap.h`.

Il scanne la bitmap afin d’allouer le nombre exact de blocs requis.

### Gestion de la fragmentation

En cas de forte fragmentation empêchant une allocation totalement contiguë :
* l’algorithme identifie la plus longue séquence libre disponible ;
* puis retourne cette allocation partielle.

Cela empêche l’allocateur d’échouer brutalement.

La boucle d’écriture s’adapte ensuite automatiquement à cette allocation partielle.

---

## 1.7 Write-time block reservation

**Statut :** Testé et fonctionnel

### Implémentation technique

Cette fonctionnalité est intégrée directement dans le flux de :
```c
ouichefs_file_get_block
```

Les variables :
* `i_reserved_start`
* `i_reserved_count`
sont maintenues en RAM pour chaque inode.

Lors d’une requête d’écriture :
1. l’allocateur tente d’abord de piocher dans la *reservation window* locale ;
2. puis sollicite la bitmap globale uniquement si nécessaire.

### Libération des blocs
La libération du fichier via le hook VFS `ouichefs_release` (ou lors de sa destruction via `ouichefs_unlink`) garantit que les blocs non consommés sont correctement restitués à la bitmap via :
```c
put_block()
```

---

## 1.7.4 Garbage collector
**Statut :** Testé et fonctionnel

### Implémentation technique
Le Garbage Collector a été isolé dans une fonction utilitaire dédiée :
```c
ouichefs_garbage_collect
```

Il est invoqué comme mécanisme de secours (*fallback*) si l’allocateur contigu échoue par manque d’espace.

### Fonctionnement
Le GC parcourt `sb->s_inodes` sous la protection stricte de :
```c
spin_lock(&sb->s_inode_list_lock)
```

### Prévention des deadlocks

Afin d’éviter tout interblocage (*deadlock*) :
* il utilise un `spin_trylock` sur chaque inode ;
* puis purge :

  * `i_reserved_count`
  * les blocs réservés inactifs.

---

## 1.8 Sysfs statistics
**Statut :** Testé et fonctionnel

### Implémentation technique
Une arborescence `kobject` a été instanciée sous :
```text
/sys/fs/ouichefs/
```

Le paramètre de réservation dynamique :
```text
reservation_size
```
est exposé à l’espace utilisateur avec les permissions :
```text
0644
```

Cela permet de moduler dynamiquement (*on-the-fly*) la taille des fenêtres d’allocation pour les futures requêtes d’écriture.

---

## 1.9 Sparse files and holes (read)
**Statut :** Testé et fonctionnel

### Implémentation technique

Notre helper :
```c
ouichefs_extent_get_block
```
intercepte les extents dont :
```c
start == 0
```

Le bloc `0` étant réservé au superbloc, il sert de marqueur logique pour identifier un *Hole*.

### Optimisation de lecture

Lors de l’opération de lecture dans `ouichefs_read` :

* l’interrogation disque via `sb_bread` est court-circuitée ;
* le buffer utilisateur est rempli de zéros via :

```c
clear_user()
```

Cela évite toute lecture disque inutile.

---

## 1.9 Sparse files and holes (write in hole)

**Statut :** Testé et fonctionnel

### Implémentation technique

La gestion de l’écriture en plein milieu d’un trou existant (*Extent Splitting*) est entièrement opérationnelle.

Lorsqu’une écriture survient dans un Hole (`start == 0`) :

1. le système calcule l’offset logique de l’insertion ;
2. les entrées existantes du tableau sont décalées vers la droite ;
3. de nouveaux slots sont libérés.

### Split du hole

Le trou originel est alors divisé en trois segments :

1. un extent pour le reliquat du Hole à gauche ;
2. un extent réel pointant vers les blocs physiques alloués ;
3. un extent pour le reliquat du Hole à droite.

### EOF holes

L’injection de trous en fin de fichier (après un `seek` au-delà de l’EOF) est également gérée.

Un extent de type Hole est inséré juste avant les nouvelles données.

---

## 1.10 Bonus: File defragmentation

**Statut :** Testé et fonctionnel

### Implémentation technique

La défragmentation est exposée via :

```c
ioctl(OUICHEFS_IOC_DEFRAG_FILE)
```

Afin de conserver un code propre et lisible dans le `switch` de l’`ioctl`, cette logique complexe a été extraite dans deux helpers :

* `ouichefs_do_defrag`
* `ouichefs_copy_defrag_data`

### Algorithme global

L’algorithme fonctionne ainsi :

1. calcul du nombre total de blocs de l’inode ;
2. allocation d’un unique chunk contigu massif ;
3. parcours des anciens extents fragmentés ;
4. copie physique des données vers les nouveaux `buffer_heads` via `memcpy` ;
5. libération des anciens blocs avec `put_block()` ;
6. reconstruction de l’index avec un unique extent contigu.

### Résultat

À la fin de l’opération :

* le fichier ne contient plus qu’un seul extent ;
* les données deviennent totalement contiguës.

---

## 1.11 MEGA Bonus: advanced block allocator

**Statut :** Non traité

### Intuition et conceptualisation

Notre allocateur actuel repose sur une recherche linéaire optimisée en :

```math
O(N)
```

sur la bitmap.

### Architecture idéale

Une architecture plus avancée (*state-of-the-art*) consisterait à remplacer cette recherche par :

* un arbre Red-Black ;
* ou un Buddy Allocator.

Le système indexerait alors les zones libres selon :

* leur taille ;
* leur adresse physique.

Lors d’un `put_block()`, l’algorithme évaluerait la contiguïté avec les nœuds adjacents.

### Gains attendus

Cela permettrait de réaliser des fusions dynamiques (*coalescence*), garantissant :

```math
O(\log N)
```

pour les allocations et les fusions.

---

# Remarques

## Tests et robustesse (*Edge cases*)

Pour valider notre implémentation, une suite complète de scripts Bash de stress test a été exécutée :

* `test_reserve.sh`
* `test_sparse.sh`

Le système démontre une forte résilience face aux *edge cases*.

## Sécurité critique

Nous avons implémenté une garde de sécurité stricte :

* dans `ouichefs_unlink`
* ainsi que lors de la troncature (`O_TRUNC`)

Cette protection garantit que :

```c
put_block()
```

ne sera jamais invoquée accidentellement sur le bloc physique `0` (le marqueur de Hole).

Cela prévient toute corruption critique du superbloc.
