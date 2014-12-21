#ifndef REQUIREMENTS_H
#define REQUIREMENTS_H

#include <string>
#include <vector>
#include <map>
#include "color.h"
#include "output.h"

class JsonObject;
class JsonArray;
class inventory;

// Denotes the id of an item type
typedef std::string itype_id;
// Denotes the id of an item quality
typedef std::string quality_id;

enum available_status {
    a_true = +1, // yes, it's available
    a_false = -1, // no, it's not available
    a_insufficent = 0, // neraly, bt not enough for tool+component
};

struct quality {
    quality_id id;
    // Translated name
    std::string name;

    typedef std::map<quality_id, quality> quality_map;
    static quality_map qualities;
    static void reset();
    static void load(JsonObject &jo);
    static std::string get_name(const quality_id &id);
    static bool has(const quality_id &id);
};

struct component {
    itype_id type;
    int count;
    // -1 means the player doesn't have the item, 1 means they do,
    // 0 means they have item but not enough for both tool and component
    mutable available_status available;
    bool recoverable;

    component() : type("null") , count(0) , available(a_false), recoverable(true)
    {
    }
    component(const itype_id &TYPE, int COUNT) : type (TYPE), count (COUNT), available(a_false), recoverable(true)
    {
    }
    component(const itype_id &TYPE, int COUNT, bool RECOVERABLE) : type (TYPE), count (COUNT), available(a_false), recoverable(RECOVERABLE)
    {
    }
    void check_consistency(const std::string &display_name) const;
};

struct tool_comp : public component {
    tool_comp() : component() { }
    tool_comp(const itype_id &TYPE, int COUNT) : component(TYPE, COUNT) { }

    void load(JsonArray &jarr);
    bool has(const inventory &crafting_inv, int batch = 1) const;
    std::string to_string(int batch = 1) const;
    std::string get_color(bool has_one, const inventory &crafting_inv, int batch = 1) const;
    bool by_charges() const;
};

struct item_comp : public component {
    item_comp() : component() { }
    item_comp(const itype_id &TYPE, int COUNT) : component(TYPE, COUNT) { }

    void load(JsonArray &jarr);
    bool has(const inventory &crafting_inv, int batch = 1) const;
    std::string to_string(int batch = 1) const;
    std::string get_color(bool has_one, const inventory &crafting_inv, int batch = 1) const;
};

struct skill_requirement {
    const Skill* skill = nullptr; // This should NEVER be null if it's loaded properly
    int minimum = 0; // Minimum skill required to use this recipe
    int difficulty = 0; // Difficulty of this recipe
    float base_success = 0.5f;
    float stat_factor = 0.125f; // How much of a bonus/penalty to skill deviating from 8 is

    skill_requirement() { }
    skill_requirement(const Skill* _skill, int _minimum, int _difficulty,
                      float _base_success = 0.5f)
        : skill(_skill), minimum(_minimum), difficulty(_difficulty),
          base_success(_base_success) { }

    void load(JsonObject &json_obj);
    bool meets_minimum(const player& _player) const;
    double success_rate(const player& _player, double difficulty_modifier = 0.0f) const;
    std::string to_string() const;
    std::string get_color(const player& _player) const;
};


struct quality_requirement {
    quality_id type;
    int level;
    mutable available_status available;

    quality_requirement() : type("UNKNOWN"), level(0), available(a_false)
    {
    }
    quality_requirement(const quality_id &_type, int _level) : type(_type),
        level(_level), available(a_false)
    {
    }

    void load(JsonArray &jarr);
    // Dummy parameters that enable using this in requirement_data::print_list
    bool has(const inventory &crafting_inv, int = 0) const;
    std::string to_string(int = 0) const;
    void check_consistency(const std::string &display_name) const;
    std::string get_color(bool has_one, const inventory &crafting_inv, int = 0) const;
};

/**
 * The *_vector members represent list of alternatives requirements:
 * alter_tool_comp_vector = { * { { a, b }, { c, d } }
 * That means: the player needs (requirement a or b) and (requirement c or d).
 *
 * Some functions in this struct use template arguments so they can be
 * applied to all three *_vector members. For example:
 * check_consistency iterates over all entries in the supplied vector and calls
 * check_consistency on each entry.
 * If called as <code>check_consistency(tools)</code> this will actually call
 * tool_comp::check_consistency. If called as
 * <code>check_consistency(qualities)</code> it will call
 * quality_requirement:check_consistency.
 *
 * Requirements (item_comp, tool_comp, quality_requirement) must have those
 * functions:
 * Load from the next entry of the json array:
 *   void load(JsonArray &jarr);
 * Check whether the player has fulfills the requirement with this crafting
 * inventory (or by mutation):
 *   bool has(const inventory &crafting_inv) const;
 * A textual representation of the requirement:
 *   std::string to_string() const;
 * Consistency check:
 *   void check_consistency(const std::string &display_name) const;
 * Color to be used for displaying the requirement. has_one is true of the
 * player fulfills an alternative requirement:
 *   std::string get_color(bool has_one, const inventory &crafting_inv) const;
*/
struct requirement_data {
        typedef std::vector< std::vector<tool_comp> > alter_tool_comp_vector;
        typedef std::vector< std::vector<item_comp> > alter_item_comp_vector;
        typedef std::vector< std::vector<quality_requirement> > alter_quali_req_vector;
        typedef std::map<std::string, skill_requirement> skill_req_map;

        alter_tool_comp_vector tools;
        alter_quali_req_vector qualities;
        alter_item_comp_vector components;
        skill_req_map skills;

        /**
         * Load @ref tools, @ref qualities and @ref components from
         * the json object. Assumes them to be in sub-objects.
         */
        void load(JsonObject &jsobj);
        /**
         * Returns a list of components/tools/qualities/skills that are not available,
         * nicely formatted for popup window or similar.
         */
        std::string list_missing() const;
        /**
         * Consistency checking
         * @param display_name the string is used when displaying a error about
         * inconsistent data (unknown item id, ...).
         */
        void check_consistency(const std::string &display_name) const;
        /**
         * Remove components (tools/items) of the given item type. Qualities are not
         * changed.
         * @returns true if any the last requirement in a list of alternatives has
         * been removed. This requirement can never be fulfilled and should be discarded.
         */
        bool remove_item(const std::string &type);

        bool can_make_with_inventory(const inventory &crafting_inv, int batch = 1) const;

        /**
         * Checks if the player or NPC meets minimum skill requirements.
         */
        bool meets_skill_requirements(const player& _player) const;
        /**
         * Computes the success rate based on skill requirements
         *
         * The formula used is `a*b*c*d` where individual terms are `(individual_rate ^ (1 / n_skills))`
         * Individual skill formula is `base_rate ^ (2^adjusted_difficulty)`, where adjusted
         * difficulty is the difference between player skill level plus stat bonus, and the optional difficulty modifier.
         *
         * @param _player The player object representing the player or an NPC
         * @param difficulty_modifier Difficulty modifier, 1.0 is equivalent of one skill level, or 8 stat points.
         * @returns Value representing success probability. If difficulty is 0, returns 1.0, if minimum requirements are not met, returns 0.0
         */
        double success_rate(const player& _player, double difficulty_modifier=0.0f) const;

        /**
         * Prints component requirement information for purposes of the crafting screen.
         */
        int print_components(WINDOW *w, int ypos, int xpos, int width, nc_color col,
                             const inventory &crafting_inv, int batch = 1) const;
        /**
         * Prints tool requirement information for purposes of the crafting screen.
         */
        int print_tools(WINDOW *w, int ypos, int xpos, int width, nc_color col,
                        const inventory &crafting_inv, int batch = 1) const;
        /**
         * Prints skill requirement information for purposes of the crafting screen.
         */
        int print_skills(WINDOW *w, int ypos, int xpos, int width, nc_color col,
                        const player& _player) const;

        /**
         * Prepares a string representing all requirements, colored by availability.
         *
         * Red for unmet requirements.
         * Yellow for partially met skill requirements.
         * Green for met requirements.
         * Gray for requirement options met by another item.
         *
         * @returns A formatted string representing requirements
         */
        std::string requirement_list(const player& _player, const inventory& crafting_inv, int batch = 1) const;

        /** @returns List of required components. See `requirement_data::requirement_list` for more info */
        std::string required_components_list(const inventory& crafting_inv, int batch = 1) const;
        /** @returns List of required tools. See `requirement_data::requirement_list` for more info */
        std::string required_tools_list(const inventory& crafting_inv, int batch = 1) const;
        /** @returns List of required skills. See `requirement_data::requirement_list` for more info */
        std::string required_skills_list(const player& _player) const;

    private:
        bool check_enough_materials(const inventory &crafting_inv, int batch = 1) const;
        bool check_enough_materials(const item_comp &comp, const inventory &crafting_inv, int batch = 1) const;

        void load_skill_requirements(JsonObject& js_obj);

        template<typename T>
        static void check_consistency(const std::vector< std::vector<T> > &vec,
                                      const std::string &display_name);
        template<typename T>
        static std::string print_missing_objs(const std::string &header,
                                              const std::vector< std::vector<T> > &objs);
        template<typename T>
        static bool has_comps(const inventory &crafting_inv, const std::vector< std::vector<T> > &vec, int batch = 1);
        template<typename T>
        static int print_list(WINDOW *w, int ypos, int xpos, int width, nc_color col,
                              const inventory &crafting_inv, const std::vector< std::vector<T> > &objs, int batch = 1);
        template<typename T>
        static std::string make_list(const inventory &crafting_inv,
                                     const std::vector< std::vector<T> > &objs, int batch = 1, bool more=false);
        template<typename T>
        static bool remove_item(const std::string &type, std::vector< std::vector<T> > &vec);
        template<typename T>
        static bool any_marked_available(const std::vector<T> &comps);
        template<typename T>
        static void load_obj_list(JsonArray &jsarr, std::vector< std::vector<T> > &objs);
        template<typename T>
        static const T *find_by_type(const std::vector< std::vector<T> > &vec, const std::string &type);
};

#endif
