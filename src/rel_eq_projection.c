/**
 @file rel_eq_projection.c Provides functions for for relational equivalences in projection
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

/**
 * ============> Optimization plan using Relational Algebra Equivalences <==============
 * Equivalence rule that apply on every equivalent expresion generated by Query optimizer
 * 
 * Rules to implement
 * Rule 1. projection comutes with selection that only uses attributes retained by the projection
 * 	p[L](s[L1](R)) = s[L1](p[L](R))
 * Rule 2. only the last in a sequence of projection operations is needed, the others can be omitted.
 *	p[L1](p[L2](...p[Ln](R)...)) = p[L1](R)
 * Rule 3a. distribution according to theta join, only if join includes attributes from L1 u L2
 *  p[L1 u L2](R1 t R2) = (p[L1](R1)) t (p[L2](R2))
 * Rule 3b. Let L1 u L2 be attributes from R1 and R2, respectively. Let L3 be attributes from R1, 
 * but are not in L1 u L2 and let L4 be attributes from R2, but are not in L1 u L2.
 *	p[L1 u L2](R1 t R2) = p[L1 u L2]((p[L1 u L3](R1)) t (p[L2 u L4](R2))) 
 * Rule 4. distribution according to union 
 *	p[L](R1 u R2) = (p[L](R1)) u (p[L](R2))
 * */

#include "rel_eq_projection.h"

/**
 * @brief Check if some set of attributes is subset of larger set, used in cascading of the projections
 * <ol>
 * <li>Tokenize set and subset of projection attributes and store each of them to it's own array</li>
 * <li>Check if the size of subset array is larger than the size of set array</li>
 * <li>if the subset array is larger return 0</li>
 * <li>else sort both arrays ascending</li>
 * <li>Compare the subset and set items at the same positions, starting from 0</li>
 * <li>if there is an item in the subset array that doesn't match attribute at the same position in the set array return 0</li>
 * <li>else continue comparing until final item in the subset array is ritched</li>
 * <li>on loop exit return EXIT_SUCCESS</li>
 * </ol>
 * @author Dino Laktašić.
 * @param AK_list_elem list_elem_set - first list element containing projection attributes 
 * @param AK_list_elem list_elem_subset -  second list element containing projection attributes 
 * @result int - returns EXIT_SUCCESS if some set of attributes is subset of larger set, else returns EXIT_FAILURE
 */
int AK_rel_eq_is_subset(AK_list_elem list_elem_set, AK_list_elem list_elem_subset) {
    int len_set, len_subset, token_id = 0, set_id, subset_id;
    char *temp_set, *temp_subset, *token_set, *token_subset, *save_token_set, *save_token_subset;
    char *tokens_set[MAX_TOKENS] = {NULL};
    char *tokens_subset[MAX_TOKENS] = {NULL};

    len_set = len_subset = 0;

    temp_set = (char *) calloc(list_elem_set->size, sizeof (char));
    temp_subset = (char *) calloc(list_elem_subset->size, sizeof (char));

    memcpy(temp_set, list_elem_set->data, list_elem_set->size);
    memcpy(temp_subset, list_elem_subset->data, list_elem_subset->size);

    dbg_messg(HIGH, REL_EQ, "RULE - is (%s) subset of set (%s) in rel_eq_projection\n", list_elem_subset->data, list_elem_set->data);

    for ((token_set = strtok_r(temp_set, ATTR_DELIMITER, &save_token_set)); token_set;
            (token_set = strtok_r(NULL, ATTR_DELIMITER, &save_token_set)), token_id++) {
        if (token_id < MAX_TOKENS - 1) {
            tokens_set[token_id] = token_set;
            len_set++;
        }
    }

    token_id = 0;
    for ((token_subset = strtok_r(temp_subset, ATTR_DELIMITER, &save_token_subset)); token_subset;
            (token_subset = strtok_r(NULL, ATTR_DELIMITER, &save_token_subset)), token_id++) {
        if (token_id < MAX_TOKENS - 1) {
            tokens_subset[token_id] = token_subset;
            len_subset++;
        }
    }

    if (len_set < len_subset) {
        dbg_messg(HIGH, REL_EQ, "RULE - failed (%s) isn't subset of set (%s)!\n", list_elem_subset->data, list_elem_set->data);
        return EXIT_FAILURE;
    }

    qsort(tokens_set, len_set, sizeof (char *), AK_strcmp);
    qsort(tokens_subset, len_subset, sizeof (char *), AK_strcmp);

    token_id = 0;

    for (subset_id = 0; tokens_subset[subset_id] != NULL; subset_id++) {
        for (set_id = 0; tokens_set[set_id] != NULL; set_id++) {
            if (strcmp(tokens_set[set_id], tokens_subset[subset_id]) == 0) {
                token_id++;
            }
        }
    }

    if (token_id != len_subset) {
        dbg_messg(HIGH, REL_EQ, "RULE - failed (%s) isn't subset of set (%s)!\n", list_elem_set->data, list_elem_subset->data);
        return EXIT_FAILURE;
    }

    //May cause troubles
    free(temp_set);
    free(temp_subset);

    dbg_messg(HIGH, REL_EQ, "RULE - succeed (%s) is subset of set (%s).\n", list_elem_subset->data, list_elem_set->data);
    return EXIT_SUCCESS;
}

/**
 * @brief Check if selection uses only attributes retained by the projection before commuting
 * <ol>
 * <li>Tokenize set of projection attributes and store them to the array</li>
 * <li>For each attribute in selection condition check if exists in array of projection attributes</li>
 * <li>if exists increment match variable and break</li>
 * <li>else continue checking until the final attribute is checked</li>
 * <li>if match variable value equals 0 than return 0</li>
 * <li>else if match variable value greater than EXIT_SUCCESS, return EXIT_FAILURE</li>
 * </ol>
 * @author Dino Laktašić.
 * @param AK_list_elem list_elem_attribs - list element containing projection data 
 * @param AK_list_elem list_elem_conds -  list element containing selection condition data
 * @result int - returns EXIT_SUCCESS if selection uses only attributes retained by projection, else returns EXIT_FAILURE
 */
int AK_rel_eq_can_commute(AK_list_elem list_elem_attribs, AK_list_elem list_elem_conds) {
    int next_chr, valid_cond_attribs, token_id = 0;
    char *token_attr, *save_token_attr, *temp_attr, *temp_cond, *temp;
    char *tokens[MAX_TOKENS] = {NULL};

    dbg_messg(HIGH, REL_EQ, "RULE - commute condition (%s) with projection (%s)\n", list_elem_conds->data, list_elem_attribs->data);

    temp = (char *) calloc(list_elem_conds->size, sizeof (char));
    temp_cond = (char *) calloc(list_elem_conds->size, sizeof (char));
    temp_attr = (char *) calloc(list_elem_attribs->size, sizeof (char));

    memcpy(temp_attr, list_elem_attribs->data, list_elem_attribs->size);

    for ((token_attr = strtok_r(temp_attr, ATTR_DELIMITER, &save_token_attr)); token_attr;
            (token_attr = strtok_r(NULL, ATTR_DELIMITER, &save_token_attr)), token_id++) {
        if (token_id < MAX_TOKENS - 1) {
            tokens[token_id] = token_attr;
        }
    }

    for (next_chr = 0; next_chr < list_elem_conds->size; next_chr++) {
        if (list_elem_conds->data[next_chr] == ATTR_ESCAPE) {
            //printf("Next attributes group at index: %i\n", next_chr);

            memcpy(temp, list_elem_conds->data + next_chr + 1, list_elem_conds->size - next_chr);
            memmove(temp_cond, temp, strcspn(temp, "`")); //ATTR_ESCAPE
            //printf("\n-->temp_cond: %s, size: %i\n", temp_cond, next_chr);
            next_chr += strlen(temp_cond) + 1;

            //printf("\tCondition: %s\n", temp_cond);
            valid_cond_attribs = 0;

            for (token_id = 0; tokens[token_id] != NULL; token_id++) {
                //printf("\tAttribute: %s\n", tokens[token_id]);

                if (strcmp(temp_cond, tokens[token_id]) == 0) {
                    valid_cond_attribs++;
                    break;
                }
            }

            if (valid_cond_attribs == 0) {
                dbg_messg(HIGH, REL_EQ, "RULE - commute condition with projection failed!\n");
                return EXIT_FAILURE;
            }
        }
    }

    free(temp);
    dbg_messg(HIGH, REL_EQ, "RULE - commute condition with projection succeed.\n");
    return EXIT_SUCCESS;
}

/**
 * @brief Get attributes for a given table and store them to the AK_list 
 * <ol>
 * <li>Get the number of attributes in a given table</li>
 * <li>Get the table header for a given table</li>
 * <li>Initialize AK_list</li>
 * <li>For each attribute in table header, insert attribute in AK_list as new AK_list element</li>
 * <li>return AK_list</li>
 * </ol>
 * @author Dino Laktašić.
 * @param char *tblName - name of the table
 * @result AK_list - returns AK_list
 */
AK_list *AK_rel_eq_get_attributes(char *tblName) {
    int next_attr;
    int num_attr = AK_num_attr(tblName);
    char *attr_name;

    //AK_header *table_header = (AK_header*)calloc(num_attr, sizeof(AK_header));
    AK_header *table_header = (AK_header *) AK_get_header(tblName);

    AK_list *list_attr = (AK_list *) malloc(sizeof (AK_list));
    AK_list_elem list_el;
    InitL(list_attr);

    for (next_attr = 0; next_attr < num_attr; next_attr++) {
        attr_name = (table_header + next_attr)->att_name;
        InsertAtEndL(TYPE_ATTRIBS, attr_name, strlen(attr_name) + 1, list_attr);
    }

    attr_name = NULL;
    free(table_header);

    return list_attr;
}

/* Another function to retreive and store table attributes, but in the array (may be usefull somewhere later)
const char *AK_rel_eq_get_attributes(char *tblName) {
        int next_attr, num_attr = AK_num_attr(tblName);
    char *attributes[MAX_ATTRIBUTES] = {NULL};
        //char *attributes = (char *)malloc(sizeof(MAX_ATTRIBUTES));
	
        //AK_header *table_header = (AK_header*)calloc(num_attr, sizeof(AK_header));
        AK_header *table_header = (AK_header *)AK_get_header(tblName);

        for (next_attr = 0; next_attr < num_attr; next_attr++) {
                attributes[next_attr] = (table_header + next_attr)->att_name;
                //printf("attribute: %s\n", attributes[next_attr]);
        }
	
        char *save_attributes = (char *)malloc(strlen(*attributes) + 1);
	
        memcpy(save_attributes, attributes, sizeof(attributes));
        return save_attributes;
} */

/**
 * @brief Filtering and returning only those attributes from list of projection attributes that exist in the given table  
 * <ol>
 * <li>Get the attributes for a given table and store them to the AK_list</li>
 * <li>Tokenize set of projection attributes and store them to the array</li>
 * <li>For each attribute in the array check if exists in the previously created AK_list</li>
 * <li>if exists append attribute to the dynamic atributes char array</li>
 * <li>return pointer to char array with stored attribute/s</li>
 * </ol>
 * @author Dino Laktašić.
 * @param char *attribs - projection attributes delimited by ";" (ATTR_DELIMITER)
 * @param char *tblName - name of the table
 * @result char * - returns filtered list of projection attributes as the AK_list
 */
char *AK_rel_eq_projection_attributes(char *attribs, char *tblName) {
    int len_tokens = 0;
    int token_id = 0;

    AK_list *list_attr = AK_rel_eq_get_attributes(tblName);
    AK_list_elem list_el;

    if (SizeL(list_attr) <= 0) {
        printf("ERROR - table (%s) doesn't exists!\n", tblName);
        return NULL;
    }

    char *token_attr, *save_token_attribs;
    char *tokens_attribs[MAX_TOKENS] = {NULL};

    char *ret_attributes = (char *) calloc(MAX_VARCHAR_LENGTH, sizeof (char));
    memset(ret_attributes, '\0', MAX_VARCHAR_LENGTH);

    char *temp_attribs = (char *) calloc(strlen(attribs), sizeof (char));

    strcpy(temp_attribs, attribs);

    dbg_messg(HIGH, REL_EQ, "\nINPUT - attributes: (%s), tblName: (%s)\n", temp_attribs, tblName);

    for ((token_attr = strtok_r(temp_attribs, ATTR_DELIMITER, &save_token_attribs)); token_attr;
            (token_attr = strtok_r(NULL, ATTR_DELIMITER, &save_token_attribs)), token_id++) {
        if (token_id < MAX_TOKENS - 1) {
            tokens_attribs[len_tokens] = token_attr;
            len_tokens++;
        }
    }

    for (token_id = 0; token_id < len_tokens; token_id++) {
        list_el = (AK_list_elem) FirstL(list_attr);

        while (list_el) {
            if (strcmp(list_el->data, tokens_attribs[token_id]) == 0) {
                //ret_attributes = (char *)realloc(ret_attributes, (strlen(list_el->data) + strlen(ret_attributes)));
                if (strlen(ret_attributes) > 0) {
                    strcat(ret_attributes, ATTR_DELIMITER);
                }
                strcat(ret_attributes, list_el->data);
            }
            list_el = list_el->next;
        }
    }

    DeleteAllL(list_attr);
    free(temp_attribs);

    dbg_messg(HIGH, REL_EQ, "RETURN - attributes for new projection (%s)\n", ret_attributes);
    return ret_attributes;
}

/**
 * @brief Filtering and returning only attributes from selection or theta_join condition 
 * @author Dino Laktašić.
 * @param AK_list_elem list_elem - list element that contains selection or theta_join condition data
 * @result char * - returns only attributes from selection or theta_join condition as the AK_list
 */
char *AK_rel_eq_collect_cond_attributes(AK_list_elem list_elem) {
    int next_chr = 0;
    int next_address = 0;
    int attr_end = -1;

    char *temp_cond = (char *) malloc(list_elem->size);
    strcpy(temp_cond, list_elem->data);

    char *attr = (char *) calloc(MAX_VARCHAR_LENGTH, sizeof (char));
    //memset(attr, '\0', MAX_VARCHAR_LENGHT);

    while (next_chr < list_elem->size) {
        if (temp_cond[next_chr] == ATTR_ESCAPE) { //'`'
            next_chr++;
            if (++attr_end) {
                attr_end = -1;
            } else {
                if (next_address > 0) {
                    strcpy(attr + next_address++, ATTR_DELIMITER);
                    //memcpy(attr + next_address++, ATTR_DELIMITER, 1);
                    //attr = (char *)realloc(attr, next_address + 1);
                }
            }
        }

        if (!attr_end) {
            strcpy(attr + next_address++, &temp_cond[next_chr]);
            //memcpy(attr + next_address++, &temp_cond[next_chr], 1);
            //attr = (char *)realloc(attr, next_address + 1);
        }
        next_chr++;
    }

    free(temp_cond);
    strcpy(attr + next_address, "\0");

    return attr;
}

/**
 * @brief Removing duplicate attributes from attributes expresion 
 * @author Dino Laktašić.
 * @param char *attribs - attributes from which to remove duplicates
 * @result char * - returns pointer to char array without duplicate attributes
 */
char *AK_rel_eq_remove_duplicates(char *attribs) {
    int next_attr = 0;
    int next_address = 0;
    int token_id = 0;
    int exist_attr;

    char *temp_attribs, *token_attr, *save_token_attribs;
    char *tokens_attribs[MAX_TOKENS] = {NULL};
    char *attr = (char *) calloc(MAX_VARCHAR_LENGTH, sizeof (char));
    memset(attr, '\0', MAX_VARCHAR_LENGTH);

    temp_attribs = (char *) calloc(strlen(attribs) + 1, sizeof (char));
    memcpy(temp_attribs, attribs, strlen(attribs));
    memcpy(temp_attribs + strlen(attribs) + 1, "\0", 1);

    for ((token_attr = strtok_r(temp_attribs, ATTR_DELIMITER, &save_token_attribs)); token_attr;
            (token_attr = strtok_r(NULL, ATTR_DELIMITER, &save_token_attribs)), token_id++) {
        if (token_id < MAX_TOKENS - 1) {
            tokens_attribs[token_id] = token_attr;
            exist_attr = 0;

            for (next_attr = 0; next_attr < token_id; next_attr++) {
                if (strcmp(tokens_attribs[next_attr], token_attr) == 0) {
                    exist_attr = 1;
                }
            }

            if (!exist_attr) {
                if (token_id > 0) {
                    strcat(attr, ATTR_DELIMITER);
                }
                strcat(attr, token_attr);
            }
        }
    }

    return attr;
}

/**
 * @brief Main function for generating RA expresion according to projection equivalence rules 
 * @author Dino Laktašić.
 * @param AK_list *list_rel_eq - RA expresion as the AK_list
 * @result AK_list - returns optimised RA expresion as the AK_list
 */
AK_list *AK_rel_eq_projection(AK_list *list_rel_eq) {
    int step; //, exit_cond[5] = {0};

    //Initialize temporary linked list
    AK_list *temp = (AK_list *) malloc(sizeof (AK_list));
    InitL(temp);

    AK_list_elem tmp, temp_elem, temp_elem_prev, temp_elem_next;
    AK_list_elem list_elem_next, list_elem = (AK_list_elem) FirstL(list_rel_eq);

    //Iterate through all the elements of RA linked list
    while (list_elem != NULL) {
        //printf("read > %s\n", list_elem->data);

        switch (list_elem->type) {

            case TYPE_OPERATOR:
                dbg_messg(LOW, REL_EQ, "\nOPERATOR '%c' SELECTED\n", list_elem->data[0]);
                dbg_messg(LOW, REL_EQ, "----------------------\n");
                temp_elem = (AK_list_elem) EndL(temp);
                temp_elem_prev = (AK_list_elem) PreviousL(temp_elem, temp);
                list_elem_next = (AK_list_elem) NextL(list_elem, list_rel_eq);

                switch (list_elem->data[0]) {
                        //Cascade of Projection p[L1](p[L2](...p[Ln](R)...)) = p[L1](R)
                        //[L1,...] < [L2,...] < [...,Ln-1,Ln]
                    case RO_PROJECTION:
                        if (temp_elem != NULL && temp_elem->type == TYPE_ATTRIBS) {
                            if (AK_rel_eq_is_subset(list_elem_next, temp_elem) == EXIT_FAILURE) {
                                InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                                InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                                dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", list_elem->data, list_elem_next->data);
                            }

                        } else {
                            InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                            InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                            dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", list_elem->data, list_elem_next->data);
                        }

                        list_elem = list_elem->next;
                        break;

                        //Commuting Selection with Projection p[L](s[L1](R)) = s[L1](p[L](R))
                    case RO_SELECTION:
                        //step = -1;
                        //check if selection uses only attributes retained by the projection before commuting
                        if (temp_elem != NULL) {
                            while (temp_elem != NULL) {
                                if (temp_elem->type == TYPE_OPERAND || temp_elem->type == TYPE_ATTRIBS) {
                                    //step++;
                                    temp_elem_prev = (AK_list_elem) PreviousL(temp_elem, temp);

                                    if (temp_elem->type == TYPE_ATTRIBS) {
                                        if ((AK_rel_eq_can_commute(temp_elem, list_elem_next) == EXIT_FAILURE) &&
                                                (temp_elem_prev->data[0] == RO_PROJECTION) && (temp_elem_prev->type == TYPE_OPERATOR)) {
                                            InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                                            InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                                            dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with condition (%s) in temp list\n", list_elem->data, list_elem_next->data);
                                            break;
                                        }
                                    }

                                    if (temp_elem_prev->data[0] == RO_PROJECTION && temp_elem_prev->type == TYPE_OPERATOR) {
                                        //if (step == 0) {
                                        InsertBeforeL(list_elem->type, list_elem->data, list_elem->size, temp_elem_prev, temp);
                                        InsertBeforeL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp_elem_prev, temp);
                                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with condition (%s) in temp list\n", list_elem->data, list_elem_next->data);
                                        break;
                                        //}
                                    }
                                } else {
                                    InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                                    InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                                    dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with condition (%s) in temp list\n", list_elem->data, list_elem_next->data);
                                    break;
                                }
                                temp_elem = (AK_list_elem) PreviousL(temp_elem, temp);
                            }

                        } else {
                            InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                            InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                            dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with condition (%s) in temp list\n", list_elem->data, list_elem_next->data);
                        }
                        list_elem = list_elem->next;
                        break;

                        //Distributing Projection over Union and Intersect p[L](R1 u R2) = (p[L](R1)) u (p[L](R2))
                    case RO_UNION:
                    case RO_INTERSECT:
                        step = -1;

                        while (temp_elem != NULL) {
                            if (temp_elem->type == TYPE_OPERAND || temp_elem->type == TYPE_ATTRIBS) {
                                step++;
                                temp_elem_prev = (AK_list_elem) PreviousL(temp_elem, temp);

                                if (temp_elem_prev->data[0] == RO_PROJECTION && temp_elem_prev->type == TYPE_OPERATOR) {
                                    if (step > 1) {
                                        tmp = temp_elem;
                                        while (tmp->type != TYPE_OPERAND) {
                                            tmp = tmp->next;
                                        }
                                        InsertAfterL(temp_elem->type, temp_elem->data, temp_elem->size, tmp, temp);
                                        InsertAfterL(temp_elem_prev->type, temp_elem_prev->data, temp_elem_prev->size, tmp, temp);
                                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", temp_elem_prev->data, temp_elem->data);
                                    }
                                    break;
                                }
                            } else {
                                break;
                            }
                            temp_elem = (AK_list_elem) PreviousL(temp_elem, temp);
                        }
                        InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted in temp list\n", list_elem->data);
                        break;

                        //Commuting Projection with Join and Cartesian Product
                    case RO_NAT_JOIN:
                        InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                        InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted in temp list\n", list_elem->data);
                        list_elem = list_elem->next;
                        break;

                    case RO_THETA_JOIN:
                        step = -1;

                        while (temp_elem != NULL) {
                            if (temp_elem->type == TYPE_OPERAND || temp_elem->type == TYPE_ATTRIBS) {
                                step++;
                                temp_elem_prev = (AK_list_elem) PreviousL(temp_elem, temp);

                                if (temp_elem_prev->data[0] == RO_PROJECTION && temp_elem_prev->type == TYPE_OPERATOR) {
                                    if (step > 1) {
                                        // If projection list is of form L = L1 u L2, where L1 only has attributes of R,
                                        // and L2 only has attributes of S, provided join condition only contains attributes
                                        // of L, Projection and Theta join commute:
                                        // p[a](R Join S) = (p[a1]R) n (p[a2]R)
                                        int has_attributes = AK_rel_eq_can_commute(temp_elem, list_elem_next);

                                        //1. Get operator and its attributes
                                        tmp = temp_elem;
                                        temp_elem_next = temp_elem->next;

                                        //2. For each attribute in projection attributes that belongs to operand attributes, append
                                        //   attribute to new projection atributes list;
                                        char *data1 = AK_rel_eq_projection_attributes(temp_elem->data, temp_elem_next->data);
                                        char *data2 = AK_rel_eq_projection_attributes(temp_elem->data, (temp_elem_next->next)->data);

                                        if (data1 != NULL && data2 != NULL) {
                                            if (!has_attributes) {
                                                temp_elem->size = strlen(data1) + 1;
                                                memcpy(temp_elem->data, data1, temp_elem->size);
                                                memset(temp_elem->data + temp_elem->size, '\0', MAX_VARCHAR_LENGTH - temp_elem->size);
                                                dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", temp_elem_prev->data, temp_elem->data);
                                            } else {
                                                //3. Insert new projection
                                                strcat(data1, ATTR_DELIMITER);
                                                strcat(data1, AK_rel_eq_projection_attributes(AK_rel_eq_collect_cond_attributes(list_elem_next), temp_elem_next->data));
                                                data1 = AK_rel_eq_remove_duplicates(data1);

                                                //memset(data2 + strlen(data1), '\0', 1);
                                                InsertAfterL(temp_elem->type, data1, strlen(data1) + 1, tmp, temp);
                                                InsertAfterL(temp_elem_prev->type, temp_elem_prev->data, temp_elem_prev->size, tmp, temp);
                                                dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", temp_elem_prev->data, data1);
                                            }

                                            free(data1);

                                            while (tmp->type != TYPE_OPERAND) {
                                                tmp = tmp->next;
                                            }

                                            if (has_attributes) {
                                                strcat(data2, ATTR_DELIMITER);
                                                strcat(data2, AK_rel_eq_projection_attributes(AK_rel_eq_collect_cond_attributes(list_elem_next), (tmp->next)->data));
                                                data2 = AK_rel_eq_remove_duplicates(data2);
                                            }
                                            memset(data2 + strlen(data2), '\0', 1);
                                            InsertAfterL(temp_elem->type, data2, strlen(data2) + 1, tmp, temp);
                                            InsertAfterL(temp_elem_prev->type, temp_elem_prev->data, temp_elem_prev->size, tmp, temp);
                                            dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with attributes (%s) in temp list\n", temp_elem_prev->data, data2);
                                        }

                                        free(data2);
                                    }
                                    break;
                                }
                            } else {
                                break;
                            }
                            temp_elem = (AK_list_elem) PreviousL(temp_elem, temp);
                        }

                        InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                        InsertAtEndL(list_elem_next->type, list_elem_next->data, list_elem_next->size, temp);
                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted with condition (%s) in temp list\n", list_elem->data, list_elem_next->data);
                        list_elem = list_elem->next;
                        break;

                    case RO_EXCEPT:
                        InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted in temp list\n", list_elem->data);
                        break;

                    case RO_RENAME:
                        InsertAtEndL(list_elem->type, list_elem->data, list_elem->size, temp);
                        dbg_messg(MIDDLE, REL_EQ, "::operator %s inserted in temp list\n", list_elem->data);
                        break;

                    default:
                        dbg_messg(LOW, REL_EQ, "Invalid operator: %s", list_elem->data);
                        break;
                }
                break;

                //additional type definition included to distinguish beetween table name and attribute/s
            case TYPE_ATTRIBS:
                //printf("::attribute '%s' inserted in the temp list\n", list_elem->data);
                break;

                //additional type definition included to distinguish beetween attribute/s and condition
            case TYPE_CONDITION:
                //printf("::condition '%s' inserted in the temp list\n", list_elem->data);
                break;

            case TYPE_OPERAND:
                dbg_messg(MIDDLE, REL_EQ, "::table_name (%s) inserted in the temp list\n", list_elem->data);
                InsertAtEndL(TYPE_OPERAND, list_elem->data, list_elem->size, temp);
                break;

            default:
                dbg_messg(LOW, REL_EQ, "Invalid type: %s", list_elem->data);
                break;
        }

        list_elem = list_elem->next;
    }

    //====================================> IMPROVMENTS <=======================================
    //Recursive RA optimization (need to implement exit condition in place of each operator, ...)
    //If there is no new changes on the list return generated AK_lists
    //int iter_cond;
    //for (iter_cond = 0; iter_cond < sizeof(exit_cond); iter_cond++) {
    //	if (exit_cond[iter_cond] == 0) {
    ////	Edit function to return collection of the AK_lists
    ////	Generate next RA expr. (new plan)
    ////	temp += remain from the list_rel_eq
    //		AK_rel_eq_projection(temp);
    //	}
    //}

    DeleteAllL(list_rel_eq);
    return temp;
}

/**
 * @brief Print AK_list to the screen 
 * @author Dino Laktašić.
 * @param AK_list *list_rel_eq - RA expresion as the AK_list
 */
void AK_print_rel_eq_projection(AK_list *list_rel_eq) {
    AK_list_elem list_elem = (AK_list_elem) FirstL(list_rel_eq);

    printf("\n");
    while (list_elem != NULL) {
        /*if (list_elem->type == TYPE_ATTRIBS || list_elem->type == TYPE_CONDITION) {
                printf("[%s]", list_elem->data);
        } else if (list_elem->type == TYPE_OPERAND){
                printf("(%s)", list_elem->data);
        } else {
                printf("%s", list_elem->data);
        }*/
        printf("Type: %i, size: %i, data: %s\n", list_elem->type, list_elem->size, list_elem->data);
        list_elem = list_elem->next;
    }
}

/**
 * @brief Test rel_eq_selection
 * @author Dino Laktašić.
 */
void AK_rel_eq_projection_test() {
    printf("rel_eq_projection.c: Present!\n");
    printf("\n********** REL_EQ_PROJECTION TEST by Dino Laktašić **********\n");

    //create header
    AK_header t_header[MAX_ATTRIBUTES];
    AK_header* temp;

    temp = (AK_header*) AK_create_header("id", TYPE_INT, FREE_INT, FREE_CHAR, FREE_CHAR);
    memcpy(t_header, temp, sizeof ( AK_header));
    temp = (AK_header*) AK_create_header("firstname", TYPE_VARCHAR, FREE_INT, FREE_CHAR, FREE_CHAR);
    memcpy(t_header + 1, temp, sizeof ( AK_header));
    temp = (AK_header*) AK_create_header("job", TYPE_VARCHAR, FREE_INT, FREE_CHAR, FREE_CHAR);
    memcpy(t_header + 2, temp, sizeof ( AK_header));
    temp = (AK_header*) AK_create_header("year", TYPE_INT, FREE_INT, FREE_CHAR, FREE_CHAR);
    memcpy(t_header + 3, temp, sizeof ( AK_header));
    temp = (AK_header*) AK_create_header("tezina", TYPE_FLOAT, FREE_INT, FREE_CHAR, FREE_CHAR);
    memcpy(t_header + 4, temp, sizeof ( AK_header));
    memset(t_header + 5, '\0', MAX_ATTRIBUTES - 5);

    //create table
    char *tblName = "profesor";

    int startAddress = AK_initialize_new_segment(tblName, SEGMENT_TYPE_TABLE, t_header);

    if (startAddress != EXIT_ERROR)
        printf("\nTABLE %s CREATED!\n", tblName);

    printf("rel_eq_projection_test: After segment initialization: %d\n", AK_num_attr(tblName));

    AK_list *expr = (AK_list *) malloc(sizeof (AK_list));
    InitL(expr);

    InsertAtEndL(TYPE_OPERATOR, "p", sizeof ("p"), expr);
    /*
     * The projection is only made up of a one or more of attributes
     */
    InsertAtEndL(TYPE_ATTRIBS, "L1;L2;L3;L4", sizeof ("L1;L2;L3;L4"), expr); //projection attribute
    InsertAtEndL(TYPE_OPERATOR, "p", sizeof ("p"), expr);
    InsertAtEndL(TYPE_ATTRIBS, "L1;L4;L3;L2;L5", sizeof ("L1;L4;L3;L2;L5"), expr);
    InsertAtEndL(TYPE_OPERATOR, "s", sizeof ("s"), expr);

    /* The selection condition is made up of a number of clauses of the form
     * <attribute name> <comparison op> <constant value> OR
     * <attribute name 1> <comparison op> <attribute name 2>
     * In the clause, the comparison operations could be one of the following: ≤, ≥, ≠, =, >, < .
     * Clauses are connected by Boolean operators : and, or , not
     */
    InsertAtEndL(TYPE_CONDITION, "`L1` 100 > `L2` 50 < OR", sizeof ("`L1` 100 > `L2` 50 < OR"), expr);
    InsertAtEndL(TYPE_OPERAND, "R", sizeof ("R"), expr);
    InsertAtEndL(TYPE_OPERAND, "S", sizeof ("S"), expr);
    InsertAtEndL(TYPE_OPERATOR, "u", sizeof ("u"), expr);

    InsertAtEndL(TYPE_OPERATOR, "p", sizeof ("p"), expr);
    InsertAtEndL(TYPE_ATTRIBS, "mbr;firstname;job", sizeof ("mbr;firstname;job"), expr);
    InsertAtEndL(TYPE_OPERAND, "student", sizeof ("student"), expr);
    InsertAtEndL(TYPE_OPERAND, "profesor", sizeof ("profesor"), expr);
    InsertAtEndL(TYPE_OPERATOR, "t", sizeof ("t"), expr);
    InsertAtEndL(TYPE_CONDITION, "`mbr` `job` =", sizeof ("`mbr` `job` ="), expr); //theta join attribute

    /*
InsertAtEndL( TYPE_OPERATOR, "s", sizeof("s"), &expr );
    InsertAtEndL( TYPE_CONDITION, "`L1` > 100", sizeof("`L1` > 100"), &expr );
InsertAtEndL( TYPE_OPERAND, "R", sizeof("R"), &expr );
    InsertAtEndL( TYPE_OPERATOR, "u", sizeof("u"), &expr );
InsertAtEndL( TYPE_OPERAND, "S", sizeof("S"), &expr );
     */
    //printf("\nRA expr. before rel_eq optimization:\n");
    //AK_print_rel_eq_projection(expr);
    AK_print_rel_eq_projection(AK_rel_eq_projection(expr));

    if (DEBUG_ALL) {
        printf("\n------------------> TEST_PROJECTION_FUNCTIONS <------------------\n\n");

        //Initialize list elements...
        //AK_list_elem list_elem_set, list_elem_subset;
        //AK_list_elem list_elem_cond, list_elem_attr;

        char *test_cond1, *test_cond2;
        char *test_table;
        char *test_attribs;

        test_table = "profesor";
        test_cond1 = "`mbr` 100 > `firstname` 'Dino' = AND `id` 1000 > OR";
        test_cond2 = "`id` 100 > `firstname` 50 < AND `job` 'teacher' = AND";
        test_attribs = "id;mbr";

        //printf("IS_SET_SUBSET_OF_LARGER_SET_TEST  : (%i)\n\n", AK_rel_eq_is_subset(list_elem_set, list_elem_subset));
        //printf("COMMUTE_PROJECTION_SELECTION_TEST : (%i)\n\n", AK_rel_eq_can_commute(list_elem_attr, list_elem_cond));
        ////printf("GET_TABLE_ATTRIBUTES_TEST       : (%s)\n\n", (AK_rel_eq_get_attributes(test_table))->data);
        //printf("GET_PROJECTION_ATTRIBUTES_TEST    : (%s)\n\n", AK_rel_eq_projection_attributes(test_attribs, test_table));
        //printf("GET_ATTRIBUTES_FROM_CONDITION_TEST: (%s)\n\n", AK_rel_eq_collect_cond_attributes(list_elem_cond));
        //printf("REMOVE_DUPLICATE_ATTRIBUTES_TEST  : (%s)\n", AK_rel_eq_remove_duplicates(test_attribs));
        /**/
    } else {
        printf("...\n");
    }

    DeleteAllL(expr);
    //dealocate variables ;)
}
