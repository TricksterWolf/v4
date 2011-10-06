/*
 * File: obj-make.c
 * Purpose: Object generation functions.
 *
 * Copyright (c) 1987-2011 Angband contributors
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "object/tvalsval.h"
#include "object/pval.h"

/*
 * The chance of inflating the requested object level (1/x).
 * Lower values yield better objects more often.
 */
#define GREAT_OBJ   20

/*
 * There is a 1/20 (5%) chance that ego-items with an inflated base-level are
 * generated when an object is turned into an ego-item (see make_ego_item).
 * As above, lower values yield better ego-items more often.
 */
#define GREAT_EGO   20

/* Define a value for minima which will be ignored. */
#define NO_MINIMUM 	255

/**
 * Select an item from an allocation table struct, using the prob3 member.
 *
 * \param total is the total of all alloc_probs in the table
 * \param max is the number of items in the table
 * \param table is the table itself
 * (Could calculate total from table, but it would be slower)
 */
static int table_pick(long total, int max, alloc_entry *table)
{
		long value = randint0(total);
		int i;

		if (!total) return -1;

		for (i = 0; i < max; i++) {
			/* Found the entry */
			if (value < table[i].prob3) return table[i].index;

			/* Decrement */
			value = value - table[i].prob3;
		}
		/* Failure */
		return -1;
}

/*** Make an ego item ***/

/**
 * This is a safe way to choose a random new flag to add to an object.
 * It takes the existing flags and an array of new flags,
 * and returns an entry from newf, or 0 if there are no
 * new flags available.
 */
static int get_new_attr(bitflag flags[OF_SIZE], bitflag newf[OF_SIZE])
{
	size_t i;
	int options = 0, flag = 0;

	for (i = of_next(newf, FLAG_START); i != FLAG_END; i = of_next(newf, i + 1))
	{
		/* skip this one if the flag is already present */
		if (of_has(flags, i)) continue;

		/* each time we find a new possible option, we have a 1-in-N chance of
		 * choosing it and an (N-1)-in-N chance of keeping a previous one */
		if (one_in_(++options)) flag = i;
	}

	return flag;
}

/**
 * Check item flags for legality. This function currently does three things:
 *  - checks slay_table for slay & brand contradictions (dedup_slays)
 *  - checks gf_table for imm/res/vuln contradictions (dedup_gf_flags)
 *  - removes all flags from ammo except slays/brands/ignore/hates
 */
void check_flags(object_type *o_ptr)
{
	bitflag f[OF_SIZE];

	dedup_slays(o_ptr->flags);

	dedup_gf_flags(o_ptr->flags);

	if (obj_is_ammo(o_ptr)) {
		create_mask(f, FALSE, OFT_SLAY, OFT_BRAND, OFT_KILL, OFT_IGNORE,
			OFT_HATES, OFT_INT, OFT_MAX);
		of_inter(o_ptr->flags, f);
	}

	return;
}


/**
 * Select an ego affix that fits the object.
 *
 * \param o_ptr is the object looking for an affix.
 * \param level is the effective generation level (not necc. dungeon level)
 */
static struct ego_item *ego_find_random(object_type *o_ptr, int level)
{
	int i, j, success = 0;
	long total = 0L;
	alloc_entry *table;
	ego_item_type *ego;

	table = C_ZNEW(z_info->a_max, alloc_entry);

	/* Go through all possible affixes and find ones legal for this item */
	for (i = 0; i < z_info->e_max; i++) {
		ego = &e_info[i];

		/* Test if this is a legal ego-item type for this object & level */
		for (j = 0; j < EGO_TVALS_MAX; j++) {
			if (o_ptr->tval == ego->tval[j] &&
					o_ptr->sval >= ego->min_sval[j] &&
					o_ptr->sval <= ego->max_sval[j] &&
					level >= ego->alloc_min[j] &&
					level <= ego->alloc_max[j]) {
				table[i].prob3 = ego->alloc_prob[j];
				table[i].index = ego->eidx;
				break;
			}
		}
		total += table[i].prob3;
	}

	/* Choose at random from all legal affixes */
	success = table_pick(total, z_info->e_max, table);

	mem_free(table);

	if (success > 0) return &e_info[success];

	/* No legal affixes */
	return NULL;
}


/**
 * Apply generation magic to an ego-item.
 */
void ego_apply_magic(object_type *o_ptr, int level)
{
	int i, j, flag, pval;
	bitflag flags[OF_SIZE];

	/* Random powers */
	for (i = 0; i < o_ptr->ego->num_randlines; i++)
		for (j = 0; j < o_ptr->ego->num_randflags[i]; j++)
			of_on(o_ptr->flags,	get_new_attr(o_ptr->flags,
				o_ptr->ego->randmask[i]));

	/* Apply extra o_ptr->ego bonuses */
	o_ptr->to_h += randcalc(o_ptr->ego->to_h, level, RANDOMISE);
	o_ptr->to_d += randcalc(o_ptr->ego->to_d, level, RANDOMISE);
	o_ptr->to_a += randcalc(o_ptr->ego->to_a, level, RANDOMISE);

	/* Apply pvals */
	for (i = 0; i < o_ptr->ego->num_pvals; i++) {
		of_copy(flags, o_ptr->ego->pval_flags[i]);
		pval = randcalc(o_ptr->ego->pval[i], level, RANDOMISE);
		for (flag = of_next(flags, FLAG_START); flag != FLAG_END;
				flag = of_next(flags, flag + 1))
			object_add_pval(o_ptr, pval, flag);
	}

	/* Apply flags */
	of_union(o_ptr->flags, o_ptr->ego->flags);

	/* Adjust AC, weight, dice and sides */
	if (o_ptr->ac && o_ptr->ego->ac_mod)
		o_ptr->ac = ((100 + o_ptr->ego->ac_mod) * o_ptr->ac) / 100;

	o_ptr->weight = ((100 + o_ptr->ego->wgt_mod) * o_ptr->weight) / 100;

	o_ptr->dd += o_ptr->ego->dd;
	if (o_ptr->dd < 1)
		o_ptr->dd = 1;

	o_ptr->ds += o_ptr->ego->ds;
	if (o_ptr->ds < 1)
		o_ptr->ds = 1;

	return;
}

/**
 * Apply minimum pvals to an ego item. Note that 0 is treated as meaning
 * "do not apply a minimum to this pval", so it leaves negative pvals alone.
 */
void ego_min_pvals(object_type *o_ptr)
{
	int i, j, flag;

	if (!o_ptr->ego) return;

	for (i = 0; i < o_ptr->num_pvals; i++)
		for (j = 0; j < o_ptr->ego->num_pvals; j++)
			for (flag = of_next(o_ptr->ego->pval_flags[j], FLAG_START);
					flag != FLAG_END;
					flag = of_next(o_ptr->ego->pval_flags[j], flag + 1))
				if (!of_has(o_ptr->flags, flag) ||
						(o_ptr->ego->min_pval[j] != NO_MINIMUM
						&& of_has(o_ptr->pval_flags[i], flag) &&
						o_ptr->pval[i] < o_ptr->ego->min_pval[j]))
					object_add_pval(o_ptr, o_ptr->ego->min_pval[j] -
						o_ptr->pval[i], flag);
}

/**
 * Apply minimum standards for ego-items.
 */
static void ego_apply_minima(object_type *o_ptr)
{
	if (!o_ptr->ego) return;

	if (o_ptr->ego->min_to_h != NO_MINIMUM &&
			o_ptr->to_h < o_ptr->ego->min_to_h)
		o_ptr->to_h = o_ptr->ego->min_to_h;
	if (o_ptr->ego->min_to_d != NO_MINIMUM &&
			o_ptr->to_d < o_ptr->ego->min_to_d)
		o_ptr->to_d = o_ptr->ego->min_to_d;
	if (o_ptr->ego->min_to_a != NO_MINIMUM &&
			o_ptr->to_a < o_ptr->ego->min_to_a)
		o_ptr->to_a = o_ptr->ego->min_to_a;

	ego_min_pvals(o_ptr);
}


/**
 * Try to find an ego-item for an object, setting o_ptr->ego if successful and
 * applying various bonuses.
 */
static void make_ego_item(object_type *o_ptr, int level)
{
	/* Cannot further improve artifacts or ego items */
	if (o_ptr->artifact || o_ptr->ego) return;

	/* Occasionally boost the generation level of an item */
	if (level > 0 && one_in_(GREAT_EGO))
		level = 1 + (level * MAX_DEPTH / randint1(MAX_DEPTH));

	/* Try to get a legal ego type for this item */
	o_ptr->ego = ego_find_random(o_ptr, level);

	/* Actually apply the ego template to the item */
	if (o_ptr->ego)
		ego_apply_magic(o_ptr, level);

	return;
}


/*** Make an artifact ***/

/**
 * Copy artifact data to a normal object, and set various slightly hacky
 * globals.
 */
void copy_artifact_data(object_type *o_ptr, const artifact_type *a_ptr)
{
	int i;

	/* Extract the data */
	for (i = 0; i < a_ptr->num_pvals; i++)
		if (a_ptr->pval[i]) {
			o_ptr->pval[i] = a_ptr->pval[i];
			of_copy(o_ptr->pval_flags[i], a_ptr->pval_flags[i]);
		}
	o_ptr->num_pvals = a_ptr->num_pvals;
	o_ptr->ac = a_ptr->ac;
	o_ptr->dd = a_ptr->dd;
	o_ptr->ds = a_ptr->ds;
	o_ptr->to_a = a_ptr->to_a;
	o_ptr->to_h = a_ptr->to_h;
	o_ptr->to_d = a_ptr->to_d;
	o_ptr->weight = a_ptr->weight;
	of_union(o_ptr->flags, a_ptr->flags);
}


/**
 * Attempt to create an artifact.  If the object is already set to be an
 * artifact, use that. If the object kind is already set, check only artifacts
 * for that kind.
 *
 * \param o_ptr is the object to turn into an artifact
 * \param level is the effective creation level
 */
static bool make_artifact(object_type *o_ptr, int level)
{
	int i, basemin = 0, basemax = 0, success = 0;
	long total = 0L;
	bool art_ok = TRUE;
	object_kind *kind;
	alloc_entry *table;
	artifact_type *a_ptr;

	/* Make sure birth no artifacts isn't set */
	if (OPT(birth_no_artifacts)) art_ok = FALSE;

	/* Special handling of quest artifacts - these override the birth option */
	if (o_ptr->artifact) {
		switch (o_ptr->artifact->aidx) {
			case ART_GROND:
			case ART_MORGOTH:
				art_ok = TRUE;
		}
	}

	if (!art_ok) return FALSE;

	/* No artifacts in the town */
	if (!p_ptr->depth) return FALSE;

	/* Create the allocation table from allowed artifacts
	   TODO: initialise it once at init and then restrict it here */
	table = C_ZNEW(z_info->a_max, alloc_entry);

	for (i = 0; !o_ptr->artifact && i < z_info->a_max; i++) {
		a_ptr = &a_info[i];

		/* Skip non-existent entries */
		if (!a_ptr->name || !a_ptr->alloc_prob) continue;

		/* Cannot make an artifact twice */
		if (a_ptr->created) continue;

		/* Enforce minimum depth (loosely) */
		if (a_ptr->alloc_min > level) {
			/* Get the out-of-depth factor */
			int d = (a_ptr->alloc_min - level) * 2;

			/* Roll for out-of-depth creation */
			if (randint0(d) != 0) continue;
		}

		/* Find the base object if we don't already have one */
		if (!o_ptr->kind) {
			kind = lookup_kind(a_ptr->tval, a_ptr->sval);

			/* Make sure we now have a base object kind */
			if (!kind) continue;

			basemin = kind->alloc_min;
			basemax = kind->alloc_max;
		} else { /* If we do have a kind, it must match */
			if (a_ptr->tval != o_ptr->tval || a_ptr->sval != o_ptr->sval)
				continue;
			basemin = o_ptr->kind->alloc_min;
			basemax = o_ptr->kind->alloc_max;
		}

		/* Enforce minimum base object level (loosely) */
		if (basemin > level) {
			/* Get the out-of-depth factor */
			int d = (basemin - level) * 3;

			/* Roll for out-of-depth creation */
			if (randint0(d) != 0) continue;
		}

		/* Enforce maximum depth (strictly) */
		if (a_ptr->alloc_max < p_ptr->depth) continue;
		if (basemax && basemax < p_ptr->depth) continue;

		/* Looks good - add this artifact to the table */
		table[i].index = a_ptr->aidx;
		table[i].prob3 = a_ptr->alloc_prob;
		total += a_ptr->alloc_prob;
	}

	/* Choose an artifact from the table, then free it */
	if (!o_ptr->artifact) {
		success = table_pick(total, z_info->a_max, table);
		if (success > 0) {
			a_ptr = &a_info[success];
			o_ptr->artifact = a_ptr;
		}
	}
	mem_free(table);

	if (o_ptr->artifact) {
		/* If we haven't got a base object yet, do it now */
		if (!o_ptr->kind) {
			kind = lookup_kind(a_ptr->tval, a_ptr->sval);

			/* Make sure we now have a base object kind */
			if (!kind) return FALSE;

			object_prep(o_ptr, kind, level, RANDOMISE);
		}
		/* Paranoia -- no artifact stacks (yet) */
		if (o_ptr->number != 1) return FALSE;

		/* Actually make the object into the chosen artifact */
		copy_artifact_data(o_ptr, o_ptr->artifact);
		o_ptr->artifact->created = 1;
		return TRUE;
	}
	/* We didn't manage to select a legal artifact */
	return FALSE;
}


/*** Apply magic to an item ***/

/*
 * Apply magic to a weapon.
 */
static void apply_magic_weapon(object_type *o_ptr, int level, int power)
{
	if (power <= 0)
		return;

	o_ptr->to_h += randint1(5) + m_bonus(5, level);
	o_ptr->to_d += randint1(5) + m_bonus(5, level);

	if (power > 1) {
		o_ptr->to_h += m_bonus(10, level);
		o_ptr->to_d += m_bonus(10, level);

		if (wield_slot(o_ptr) == INVEN_WIELD || obj_is_ammo(o_ptr))
			/* Super-charge the damage dice */
			while ((o_ptr->dd * o_ptr->ds > 0) &&
					one_in_(10L * o_ptr->dd * o_ptr->ds))
				o_ptr->dd++;
	}
}


/*
 * Apply magic to armour
 */
static void apply_magic_armour(object_type *o_ptr, int level, int power)
{
	if (power <= 0)
		return;

	o_ptr->to_a += randint1(5) + m_bonus(5, level);
	if (power > 1)
		o_ptr->to_a += m_bonus(10, level);
}


/**
 * Wipe an object clean and make it a standard object of the specified kind.
 */
void object_prep(object_type *o_ptr, struct object_kind *k, int lev,
		aspect rand_aspect)
{
	int i, flag, x;
	bitflag flags[OF_SIZE];

	/* Clean slate */
	WIPE(o_ptr, object_type);

	/* Assign the kind and copy across data */
	o_ptr->kind = k;
	o_ptr->tval = k->tval;
	o_ptr->sval = k->sval;
	o_ptr->ac = k->ac;
	o_ptr->dd = k->dd;
	o_ptr->ds = k->ds;
	o_ptr->weight = k->weight;

	/* Default number */
	o_ptr->number = 1;

	/* Apply pvals and then copy flags */
    for (i = 0; i < k->num_pvals; i++) {
        of_copy(flags, k->pval_flags[i]);
        x = randcalc(k->pval[i], lev, rand_aspect);
        for (flag = of_next(flags, FLAG_START); flag != FLAG_END;
                flag = of_next(flags, flag + 1))
            object_add_pval(o_ptr, x, flag);
    }
	of_copy(o_ptr->flags, k->base->flags);
	of_union(o_ptr->flags, k->flags);

	/* Assign charges (wands/staves only) */
	if (o_ptr->tval == TV_WAND || o_ptr->tval == TV_STAFF)
		o_ptr->pval[DEFAULT_PVAL] = randcalc(k->charge, lev, rand_aspect);

	/* Assign flagless pval for food or oil */
	if (o_ptr->tval == TV_FOOD || o_ptr->tval == TV_POTION ||
			o_ptr->tval == TV_FLASK)
		o_ptr->pval[DEFAULT_PVAL]
			= randcalc(k->pval[DEFAULT_PVAL], lev, rand_aspect);

	/* Default fuel for lamps */
	if (o_ptr->tval == TV_LIGHT) {
		if (o_ptr->sval == SV_LIGHT_TORCH)
			o_ptr->timeout = DEFAULT_TORCH;
		else if (o_ptr->sval == SV_LIGHT_LANTERN)
			o_ptr->timeout = DEFAULT_LAMP;
	}

	/* Default magic */
	o_ptr->to_h = randcalc(k->to_h, lev, rand_aspect);
	o_ptr->to_d = randcalc(k->to_d, lev, rand_aspect);
	o_ptr->to_a = randcalc(k->to_a, lev, rand_aspect);
}


/**
 * Applying magic to an object, which includes creating ego-items, and applying
 * random bonuses,
 *
 * The `good` argument forces the item to be at least `good`, and the `great`
 * argument does likewise.  Setting `allow_artifacts` to TRUE allows artifacts
 * to be created here.
 *
 * If `good` or `great` are not set, then the `lev` argument controls the
 * quality of item.
 *
 * Returns 0 if a normal object, 1 if a good object, 2 if an ego item, 3 if an
 * artifact.
 */
s16b apply_magic(object_type *o_ptr, int lev, bool allow_artifacts,
		bool good, bool great)
{
	int i;
	s16b power = 0;

	/* Chance of being `good` and `great` */
	int good_chance = (lev + 2) * 3;
	int great_chance = MIN(lev / 4 + lev, 50);

	/* Roll for "good" */
	if (good || (randint0(100) < good_chance)) {
		power = 1;

		/* Roll for "great" */
		if (great || (randint0(100) < great_chance))
			power = 2;
	}

	/* Roll for artifact creation - n.b. this is now only used for objects
	   whose kind was predetermined (i.e. if we were called directly and not
	   via make_object) */
	if (allow_artifacts) {
		int rolls = 0;

		/* Get one roll if excellent */
		if (power >= 2) rolls = 1;

		/* Get four rolls if forced great */
		if (great) rolls = 4;

		/* Roll for artifacts if allowed */
		for (i = 0; i < rolls; i++)
			if (make_artifact(o_ptr, lev)) return 3;
	}

	/* Try to make an ego item */
	if (power == 2)
		make_ego_item(o_ptr, lev);

	/* Apply magic */
	switch (o_ptr->tval)
	{
		case TV_DIGGING:
		case TV_HAFTED:
		case TV_POLEARM:
		case TV_SWORD:
		case TV_BOW:
		case TV_SHOT:
		case TV_ARROW:
		case TV_BOLT:
			apply_magic_weapon(o_ptr, lev, power);
			break;

		case TV_DRAG_ARMOR:
		case TV_HARD_ARMOR:
		case TV_SOFT_ARMOR:
		case TV_SHIELD:
		case TV_HELM:
		case TV_CROWN:
		case TV_CLOAK:
		case TV_GLOVES:
		case TV_BOOTS:
			apply_magic_armour(o_ptr, lev, power);
			break;

		case TV_RING:
			if (o_ptr->sval == SV_RING_SPEED) {
				/* Super-charge the ring */
				while (one_in_(2))
					o_ptr->pval[which_pval(o_ptr, OF_SPEED)]++;
			}
			break;

		case TV_CHEST:
			/* Hack -- skip ruined chests */
			if (o_ptr->kind->level <= 0) break;

			/* Hack -- pick a "difficulty" */
			o_ptr->pval[DEFAULT_PVAL] = randint1(o_ptr->kind->level);

			/* Never exceed "difficulty" of 55 to 59 */
			if (o_ptr->pval[DEFAULT_PVAL] > 55)
				o_ptr->pval[DEFAULT_PVAL] = (s16b)(55 + randint0(5));

			break;
	}

	/* Apply minima from ego items if necessary */
	ego_apply_minima(o_ptr);

	return power;
}


/*** Generate a random object ***/

/**
 * Test whether an object is intrinsically good.
 *
 * Note that this test only applies to the object *kind*, so it is
 * possible to choose a kind which is good, and then later cause
 * the actual object to be cursed.  We do explicitly forbid objects
 * which are known to be boring or which start out somewhat damaged.
 */
static bool kind_is_good(const object_kind *kind)
{
		if (!of_has(kind->flags, OF_GOOD) &&
				!of_has(kind->base->flags, OF_GOOD)) return FALSE;
		if (randcalc(kind->to_a, 0, MINIMISE) < 0) return FALSE;
		if (randcalc(kind->to_h, 0, MINIMISE) < 0) return FALSE;
		if (randcalc(kind->to_d, 0, MINIMISE) < 0) return FALSE;
		return TRUE;
}


/** Arrays holding an index of objects to generate for a given level */
static u32b obj_total[MAX_DEPTH];
static byte *obj_alloc;

static u32b obj_total_great[MAX_DEPTH];
static byte *obj_alloc_great;

/* Don't worry about probabilities for anything past dlev100 */
#define MAX_O_DEPTH		100

/*
 * Using k_info[], init rarity data for the entire dungeon.
 */
bool init_obj_alloc(void)
{
	int k_max = z_info->k_max;
	int item, lev;


	/* Free obj_allocs if allocated */
	FREE(obj_alloc);

	/* Allocate and wipe */
	obj_alloc = C_ZNEW((MAX_O_DEPTH + 1) * k_max, byte);
	obj_alloc_great = C_ZNEW((MAX_O_DEPTH + 1) * k_max, byte);

	/* Wipe the totals */
	C_WIPE(obj_total, MAX_O_DEPTH + 1, u32b);
	C_WIPE(obj_total_great, MAX_O_DEPTH + 1, u32b);


	/* Init allocation data */
	for (item = 1; item < k_max; item++)
	{
		const object_kind *kind = &k_info[item];

		int min = kind->alloc_min;
		int max = kind->alloc_max;

		/* If an item doesn't have a rarity, move on */
		if (!kind->alloc_prob) continue;

		/* Go through all the dungeon levels */
		for (lev = 0; lev <= MAX_O_DEPTH; lev++)
		{
			int rarity = kind->alloc_prob;

			/* Save the probability in the standard table */
			if ((lev < min) || (lev > max)) rarity = 0;
			obj_total[lev] += rarity;
			obj_alloc[(lev * k_max) + item] = rarity;

			/* Save the probability in the "great" table if relevant */
			if (!kind_is_good(kind)) rarity = 0;
			obj_total_great[lev] += rarity;
			obj_alloc_great[(lev * k_max) + item] = rarity;
		}
	}

	return TRUE;
}


/*
 * Free object allocation info.
 */
void free_obj_alloc(void)
{
	FREE(obj_alloc);
	FREE(obj_alloc_great);
}


/*
 * Choose an object kind given a dungeon level to choose it for.
 */
object_kind *get_obj_num(int level, bool good)
{
	/* This is the base index into obj_alloc for this dlev */
	size_t ind, item;
	u32b value;

	/* Occasional level boost */
	if ((level > 0) && one_in_(GREAT_OBJ))
	{
		/* What a bizarre calculation */
		level = 1 + (level * MAX_O_DEPTH / randint1(MAX_O_DEPTH));
	}

	/* Paranoia */
	level = MIN(level, MAX_O_DEPTH);
	level = MAX(level, 0);

	/* Pick an object */
	ind = level * z_info->k_max;

	if (!good)
	{
		value = randint0(obj_total[level]);
		for (item = 1; item < z_info->k_max; item++)
		{
			/* Found it */
			if (value < obj_alloc[ind + item]) break;

			/* Decrement */
			value -= obj_alloc[ind + item];
		}
	}
	else
	{
		value = randint0(obj_total_great[level]);
		for (item = 1; item < z_info->k_max; item++)
		{
			/* Found it */
			if (value < obj_alloc_great[ind + item]) break;

			/* Decrement */
			value -= obj_alloc_great[ind + item];
		}
	}


	/* Return the item index */
	return objkind_byid(item);
}


/**
 * Attempt to make an object
 *
 * \param c is the current dungeon level.
 * \param j_ptr is the object struct to be populated.
 * \param lev is the creation level of the object (not necessarily == depth).
 * \param good is whether the object is to be good
 * \param great is whether the object is to be great
 * \param value is the value to be returned to the calling function
 *
 * Returns the whether or not creation worked.
 */
bool make_object(struct cave *c, object_type *j_ptr, int lev, bool good,
	bool great, s32b *value)
{
	int base, art;
	object_kind *kind;

	if (great)
		art = 10;
	else if (good)
		art = 100;
	else
		art = 1000;

	/* Try to make an artifact */
	if (one_in_(art)) {
		if (make_artifact(j_ptr, lev)) {
			if (value)
				*value = object_value_real(j_ptr, 1, FALSE, TRUE);
			return TRUE;
		}
	}

	/* Base level for the object */
	base = (good ? (lev + 10) : lev);

	/* Get the object, prep it and apply magic */
	kind = get_obj_num(base, good || great);
	if (!kind) return FALSE;
	object_prep(j_ptr, kind, lev, RANDOMISE);
	apply_magic(j_ptr, lev, FALSE, good, great);

	/* Generate multiple items */
	if (kind->gen_mult_prob >= randint1(100))
		j_ptr->number = randcalc(kind->stack_size, lev, RANDOMISE);

	if (j_ptr->number >= MAX_STACK_SIZE)
		j_ptr->number = MAX_STACK_SIZE - 1;

	/* Return value, increased for uncursed out-of-depth objects */
	if (value)
		*value = object_value_real(j_ptr, j_ptr->number, FALSE, TRUE);

	if (!cursed_p(j_ptr->flags) && (kind->alloc_min > c->depth)) {
		if (value) *value = (kind->alloc_min - c->depth) * (*value / 5);
	}

	return TRUE;
}


/*** Make a gold item ***/

/* The largest possible average gold drop at max depth with biggest spread */
#define MAX_GOLD_DROP     (3 * MAX_DEPTH + 30)

/*
 * Make a money object
 */
void make_gold(object_type *j_ptr, int lev, int coin_type)
{
	int sval;

	/* This average is 20 at dlev0, 105 at dlev40, 220 at dlev100. */
	/* Follows the formula: y=2x+20 */
	s32b avg = 2*lev + 20;
	s32b spread = lev + 10;
	s32b value = rand_spread(avg, spread);

	/* Increase the range to infinite, moving the average to 110% */
	while (one_in_(100) && value * 10 <= MAX_SHORT)
		value *= 10;

	/* Pick a treasure variety scaled by level, or force a type */
	if (coin_type != SV_GOLD_ANY)
		sval = coin_type;
	else
		sval = (((value * 100) / MAX_GOLD_DROP) * SV_GOLD_MAX) / 100;

	/* Do not create illegal treasure types */
	if (sval >= SV_GOLD_MAX) sval = SV_GOLD_MAX - 1;

	/* Prepare a gold object */
	object_prep(j_ptr, lookup_kind(TV_GOLD, sval), lev, RANDOMISE);

	/* If we're playing with no_selling, increase the value */
	if (OPT(birth_no_selling) && p_ptr->depth)
		value = value * MIN(5, p_ptr->depth);

	/* Cap gold at max short (or alternatively make pvals s32b) */
	if (value > MAX_SHORT)
		value = MAX_SHORT;

	j_ptr->pval[DEFAULT_PVAL] = value;
}
