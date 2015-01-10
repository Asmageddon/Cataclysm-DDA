#include "requirements.h"
#include "json.h"
#include "translations.h"
#include "game.h"
#include "inventory.h"
#include "output.h"
#include "itype.h"
#include <sstream>
#include "calendar.h"
#include <cmath>

quality::quality_map quality::qualities;

void quality::reset()
{
    qualities.clear();
}

void quality::load( JsonObject &jo )
{
    quality qual;
    qual.id = jo.get_string( "id" );
    qual.name = _( jo.get_string( "name" ).c_str() );
    qualities[qual.id] = qual;
}

std::string quality::get_name( const std::string &id )
{
    const auto a = qualities.find( id );
    if( a != qualities.end() ) {
        return a->second.name;
    }
    return id;
}

bool quality::has( const std::string &id )
{
    return qualities.count( id ) > 0;
}

std::string quality_requirement::to_string(int) const
{
    return string_format( _("tool with %s of %d or more."),
                          quality::get_name( type ).c_str(), level );
}

void skill_requirement::load( JsonObject &json_obj )
{
    // Two examples: {"skill": "tailor", "difficulty": 3}, {"skill": "fabrication", "min": 2, "difficulty": 3, "base_success": 0.7}
    auto skill_name = json_obj.get_string("skill");
    skill = Skill::skill(skill_name);
    difficulty = json_obj.get_int("difficulty"); // Mandatory
    minimum = json_obj.has_member("min") ? json_obj.get_int("min") : difficulty;

    if(json_obj.has_member("base_success")) {
        base_success = json_obj.get_float("base_success");
    }
    if(json_obj.has_member("stat_factor")) {
        stat_factor = json_obj.get_float("stat_factor");
    }
}

skill_requirement skill_requirement::from_json( JsonObject &jsobj )
{
    skill_requirement obj = skill_requirement();
    obj.load(jsobj);
    return obj;
}

// Skill requirement class
bool skill_requirement::meets_minimum(const player& _player) const
{
    return (_player.get_skill_level(skill) >= minimum);
}

double skill_requirement::success_rate(const player& _player, double difficulty_modifier) const
{
    if (difficulty == 0) return 1.0; // It's impossible to fail level 0 recipes
    double relative_difficulty = _player.get_adjusted_skill_level(skill) - difficulty;
    int stat = _player.int_cur; // Only intelligence for now
    double stat_bonus = (stat - 8) * stat_factor;

    double base_rate = base_success;
    double exponent = pow(2, relative_difficulty - difficulty_modifier + stat_bonus);

    return 1.0 - pow(1.0 - base_rate, exponent);
}

std::string skill_requirement::to_string() const
{
    return string_format(_("level %d %s"), difficulty, skill->name().c_str());
}

std::string skill_requirement::get_color(const player& _player) const
{
    auto skill_level = _player.get_skill_level(skill);

    if (skill_level < minimum) return "red";
    if (skill_level < difficulty) return "yellow";

    return "green";
}

template<typename T> T legacy_req_load( JsonArray &jsarr, bool is_tool = false );

template<> item_requirement legacy_req_load( JsonArray &jsarr, bool is_tool) {
    if(is_tool) {
        item_requirement req = item_requirement();
        if( jsarr.test_string() ) {
            // constructions uses this format: [ "tool", ... ]. Ugh.
            req.type = jsarr.next_string();
            req.count = 0;
        } else {
            JsonArray comp = jsarr.next_array();
            req.type = comp.get_string( 0 );
            int count = comp.get_int( 1 );
            if(count < 0) {
                req.count = count * -1; req.by_charges = false;
            } else {
                req.count = count; req.by_charges = true;
            }
        }

        return req;
    } else {
        item_requirement req = item_requirement();
        JsonArray comp = jsarr.next_array();
        req.type = comp.get_string( 0 );
        req.count = comp.get_int( 1 );

        if(comp.size() > 2) {
            req.recoverable = comp.get_string(2) == "NO_RECOVER" ? false : true;
        }

        return req;
    }
}

template<> quality_requirement legacy_req_load( JsonArray &jsarr, bool ) {
    quality_requirement req = quality_requirement();
    JsonObject quality_data = jsarr.next_object();
    req.type = quality_data.get_string( "id" );
    req.level = quality_data.get_int( "level", 1 );

    return req;
}

template<typename T>
void requirement_data::load_obj_list(JsonArray &jsarr, std::vector< std::vector<T> > &objs, bool is_tool)
{
    while (jsarr.has_more()) {
        if(jsarr.test_array()) {
            std::vector<T> choices;
            JsonArray ja = jsarr.next_array();
            while (ja.has_more()) {
                choices.push_back(legacy_req_load<T>(ja, is_tool));
            }
            if( !choices.empty() ) {
                objs.push_back( choices );
            }
        } else {
            // tool qualities don't normally use a list of alternatives
            // each quality is mandatory.
            objs.push_back(std::vector<T>(1));
            objs.back()[0] = legacy_req_load<T>(jsarr, is_tool);
        }
    }
}

template<typename T>
void load_requirement_alternative_list(JsonObject &jsobj, std::string from_field,
                                       std::vector< std::vector<T> > &into)
{
    if(!jsobj.has_array(from_field)) return;

    JsonArray jsarr = jsobj.get_array(from_field);
    while(jsarr.has_more())
    {
        into.push_back(std::vector<T>());
        if(jsarr.test_array()) {
            JsonArray subarray = jsarr.next_array();
            while(subarray.has_more()) {
                JsonObject req_obj = subarray.next_object();
                auto req = T::from_json(req_obj);
                into.back().push_back(req);
            }
        } else {
            JsonObject req_obj = jsarr.next_object();
            auto req = T::from_json(req_obj);
            into.back().push_back(req);
        }
    }
}

void requirement_data::load( JsonObject &jsobj )
{
    if(jsobj.has_object("requirements")) {
        auto req_subobj = jsobj.get_object("requirements");
        load_requirement_alternative_list(req_subobj, "components", components);
        load_requirement_alternative_list(req_subobj, "qualities", qualities);
        load_requirement_alternative_list(req_subobj, "tools", tools);
        if(req_subobj.has_array("skills")) {
            JsonArray jsarr = req_subobj.get_array("skills");
            while(jsarr.has_more()) {
                JsonObject skill_req_obj = jsarr.next_object();
                skill_requirement req = skill_requirement::from_json(skill_req_obj);
                skills[req.skill->ident()] = req;
            }
        }
    } else {
        // This is a horrid hack, and by itself a good enough reason to deprecate old format
        JsonArray jsarr;
        if(jsobj.has_array("components")) {
            jsarr = jsobj.get_array( "components" );
            load_obj_list( jsarr, components, false );
        }
        if(jsobj.has_array("qualities")) {
            jsarr = jsobj.get_array( "qualities" );
            load_obj_list( jsarr, qualities );
        }
        if(jsobj.has_array("tools")) {
            jsarr = jsobj.get_array( "tools" );
            load_obj_list( jsarr, tools, true );
        }
        load_skill_requirements(jsobj);
    }
}

void requirement_data::load_skill_requirements(JsonObject& js_obj) {
    // Load data files from before the recipe minimum/difficulty skill requirement change
    // TODO: This should be phased out once/if data files change
    std::string skill_name = js_obj.get_string("skill_used", "");
    if (skill_name.length() != 0) {
        auto skill = Skill::skill(skill_name);
        auto req = skill_requirement();
        req.skill = skill;
        req.difficulty = js_obj.get_int( "difficulty" );;
        req.minimum = req.difficulty;
        skills[skill_name] = req;
    }

    JsonArray skills_array = js_obj.get_array("skills_required");
    if (!skills_array.empty()) {
        // could be a single requirement, or multiple
        if( skills_array.has_array(0) ) {
            while (skills_array.has_more()) {
                JsonArray ja = skills_array.next_array();
                auto skill = Skill::skill(ja.get_string(0));
                auto req = skill_requirement();
                req.skill = skill;
                req.difficulty = ja.get_int(1);
                req.minimum = ja.get_int(1);
                skills[ja.get_string(0)] = req;
            }
        } else {
            auto skill = Skill::skill(skills_array.get_string(0));
            auto req = skill_requirement();
            req.skill = skill;
            req.difficulty = skills_array.get_int(1);
            req.minimum = skills_array.get_int(1);
            req.base_success = 0.55f;
            skills[skills_array.get_string(0)] = req;
        }
    }
}

template<typename T>
bool requirement_data::any_marked_available( const std::vector<T> &comps )
{
    for( const auto &comp : comps ) {
        if( comp.available == a_true ) {
            return true;
        }
    }
    return false;
}

template<typename T>
std::string requirement_data::print_missing_objs(const std::string &header,
        const std::vector< std::vector<T> > &objs)
{
    std::ostringstream buffer;
    for( const auto &list : objs ) {
        if( any_marked_available( list ) ) {
            continue;
        }
        if( !buffer.str().empty() ) {
            buffer << "\n" << _("and ");
        }
        for( auto it = list.begin(); it != list.end(); ++it ) {
            if( it != list.begin() ) {
                buffer << _(" or ");
            }
            buffer << it->to_string();
        }
    }
    if (buffer.str().empty()) {
        return std::string();
    }
    return header + "\n" + buffer.str() + "\n";
}

std::string requirement_data::list_missing() const
{
    std::ostringstream buffer;
    buffer << print_missing_objs(_("These tools are missing:"), tools);
    buffer << print_missing_objs(_("These tools are missing:"), qualities);
    buffer << print_missing_objs(_("Those components are missing:"), components);
    return buffer.str();
}

void quality_requirement::check_consistency( const std::string &display_name ) const
{
    if( !quality::has( type ) ) {
        debugmsg( "Unknown quality %s in %s", type.c_str(), display_name.c_str() );
    }
}

template<typename T>
void requirement_data::check_consistency( const std::vector< std::vector<T> > &vec,
                                      const std::string &display_name )
{
    for( const auto &list : vec ) {
        for( const auto &comp : list ) {
            comp.check_consistency( display_name );
        }
    }
}

void requirement_data::check_consistency( const std::string &display_name ) const
{
    check_consistency(tools, display_name);
    check_consistency(components, display_name);
    check_consistency(qualities, display_name);
}

int requirement_data::print_components( WINDOW *w, int ypos, int xpos, int width, nc_color col,
                                    const inventory &crafting_inv, int batch ) const
{
    if( components.empty() ) {
        return 0;
    }
    mvwprintz( w, ypos, xpos, col, _( "Components required:" ) );
    return print_list( w, ypos + 1, xpos, width, col, crafting_inv, components, batch ) + 1;
}

template<typename T>
int requirement_data::print_list( WINDOW *w, int ypos, int xpos, int width, nc_color col,
                              const inventory &crafting_inv, const std::vector< std::vector<T> > &objs,
                              int batch )
{
    const int oldy = ypos;
    for( const auto &comp_list : objs ) {
        const bool has_one = any_marked_available( comp_list );
        std::ostringstream buffer;
        for( auto a = comp_list.begin(); a != comp_list.end(); ++a ) {
            if( a != comp_list.begin() ) {
                buffer << "<color_white> " << _( "OR" ) << "</color> ";
            }
            const std::string col = a->get_color( has_one, crafting_inv, batch );
            buffer << "<color_" << col << ">" << a->to_string(batch) << "</color>";
        }
        mvwprintz( w, ypos, xpos, col, "> " );
        ypos += fold_and_print( w, ypos, xpos + 2, width - 2, col, buffer.str() );
    }
    return ypos - oldy;
}

template<typename T>
std::string requirement_data::make_list(const inventory &crafting_inv,
    const std::vector< std::vector<T> > &objs,
    int batch, bool colored)
{
    std::ostringstream buffer;

    bool first = true;
    for( const auto &comp_list : objs ) {
        if(first) first = false; else buffer << ", ";

        const bool has_one = any_marked_available( comp_list );
        bool first=true;
        for( auto &req: comp_list ) {
            if(first) {
                first = false;
            } else {
                auto or_str = _("OR");
                if(colored) buffer << "<color_white> " << or_str << "</color> ";
                else buffer << " " << or_str << " ";
            }
            if (colored) {
                const std::string col = req.get_color( has_one, crafting_inv, batch );
                buffer << "<color_" << col << ">" << req.to_string(batch) << "</color>";
            } else {
                buffer << req.to_string(batch);
            }
        }
    }
    return buffer.str();
}

int requirement_data::print_tools( WINDOW *w, int ypos, int xpos, int width, nc_color col,
                               const inventory &crafting_inv, int batch ) const
{
    const int oldy = ypos;
    mvwprintz( w, ypos, xpos, col, _( "Tools required:" ) );
    ypos++;
    if( tools.empty() && qualities.empty() ) {
        mvwprintz( w, ypos, xpos, col, "> " );
        mvwprintz( w, ypos, xpos + 2, c_green, _( "NONE" ) );
        ypos++;
        return ypos - oldy;
    }
    ypos += print_list(w, ypos, xpos, width, col, crafting_inv, qualities);
    ypos += print_list(w, ypos, xpos, width, col, crafting_inv, tools, batch);
    return ypos - oldy;
}

// Utility function for printing a vector of vectors of component requirements
int requirement_data::print_skills(WINDOW *w, int ypos, int xpos, int width, nc_color col,
                                const player& _player) const
{
    const int oldy = ypos;
    mvwprintz( w, ypos, xpos, col, _( "Skills used:" ) );
    ypos++;
    if( skills.empty() ) {
        mvwprintz( w, ypos, xpos, col, "> " );
        mvwprintz( w, ypos, xpos + 2, c_green, _( "NONE" ) );
        ypos++;
        return ypos - oldy;
    }
    for(auto &iter: skills) {
        auto requirement = iter.second;
        std::ostringstream buffer;

        auto skill_level = _player.get_skill_level(requirement.skill);
        auto skill_name = requirement.skill->name();

        buffer << "> ";
        buffer << "<color_" << requirement.get_color(_player) << ">";
        buffer << requirement.to_string() << "</color>";
        ypos += fold_and_print( w, ypos, xpos, width, col, buffer.str() );
    }
    return ypos - oldy;
}

// Utility function for printing a vector of vectors of component requirements
std::string requirement_data::requirement_list(const player& _player, const inventory& crafting_inv, int batch, bool colored) const
{
    std::ostringstream buffer;

    std::string components_str = required_components_list(crafting_inv, batch, colored);
    std::string tools_str = required_tools_list(crafting_inv, batch, colored);
    std::string skills_str = required_skills_list(_player, colored);

    buffer << components_str;
    if (components_str.length() > 0 && (tools_str.length() > 0 || skills_str.length() > 0) ) {
        buffer << ", ";
    }
    buffer << tools_str;
    if (tools_str.length() > 0 && skills_str.length() > 0 ) {
        buffer << ", ";
    }
    buffer << skills_str;

    return buffer.str();
}

std::string requirement_data::required_components_list(const inventory& crafting_inv, int batch, bool colored) const
{
    return make_list(crafting_inv, components, batch, colored);
}

std::string requirement_data::required_tools_list(const inventory& crafting_inv, int batch, bool colored) const
{
    std::ostringstream buffer;
    auto qualities_str = make_list(crafting_inv, qualities, batch, colored);
    auto tools_str = make_list(crafting_inv, tools, batch, colored);
    buffer << qualities_str;
    if ((qualities_str.length() > 0) && (tools_str.length() > 0)) buffer << ", ";
    buffer << tools_str;

    return buffer.str();
}

std::string requirement_data::required_skills_list(const player& _player, bool colored) const
{
    std::ostringstream skills_as_stream;
    bool first = true;
    for( auto &iter: skills ) {
        if(first) first = false; else skills_as_stream << ", ";
        auto requirement = iter.second;
        if (colored) {
            skills_as_stream << "<color_" << requirement.get_color(_player) << ">";
            skills_as_stream << requirement.to_string() << "</color>";
        } else {
            skills_as_stream << requirement.to_string();
        }
    }
    return skills_as_stream.str();
}

bool requirement_data::can_make_with_inventory( const inventory &crafting_inv, int batch ) const
{
    bool retval = true;
    // All functions must be called to update the available settings in the components.
    if( !has_comps( crafting_inv, qualities ) ) {
        retval = false;
    }
    if( !has_comps( crafting_inv, tools, batch ) ) {
        retval = false;
    }
    if( !has_comps( crafting_inv, components, batch ) ) {
        retval = false;
    }
    if( !check_enough_materials( crafting_inv, batch ) ) {
        retval = false;
    }
    return retval;
}

bool requirement_data::meets_skill_requirements(const player& _player) const
{
    for(auto &requirement: skills) {
        if( !requirement.second.meets_minimum(_player) ) return false;
    }
    return true;
}

double requirement_data::success_rate(const player& _player, double difficulty_modifier) const
{
    int n = skills.size();

    double compound_rate = 1.0f;
    for(auto &requirement: skills) {
        double skill_success_rate = requirement.second.success_rate(_player, difficulty_modifier);
        double adjusted_rate = pow(skill_success_rate, 1.0f / n);
        compound_rate *= adjusted_rate;
    }

    return compound_rate;
}

template<typename T>
bool requirement_data::has_comps( const inventory &crafting_inv,
                              const std::vector< std::vector<T> > &vec,
                              int batch )
{
    bool retval = true;
    for( const auto &set_of_tools : vec ) {
        bool has_tool_in_set = false;
        for( const auto &tool : set_of_tools ) {
            if( tool.has( crafting_inv, batch ) ) {
                tool.available = a_true;
            } else {
                tool.available = a_false;
            }
            has_tool_in_set = has_tool_in_set || tool.available == a_true;
        }
        if( !has_tool_in_set ) {
            retval = false;
        }
    }
    return retval;
}

void quality_requirement::load( JsonObject &jsobj )
{
    type = jsobj.get_string("quality");
    level = jsobj.get_int("level");
}

quality_requirement quality_requirement::from_json( JsonObject &jsobj )
{
    quality_requirement obj = quality_requirement();
    obj.load(jsobj);
    return obj;
}

bool quality_requirement::has( const inventory &crafting_inv, int ) const
{
    return crafting_inv.has_items_with_quality( type, level, 1 );
}

std::string quality_requirement::get_color( bool, const inventory &, int ) const
{
    return available == a_true ? "green" : "red";
}

void item_requirement::check_consistency( const std::string &display_name ) const
{
    if( !item::type_is_defined( type ) ) {
        debugmsg( "%s in %s is not a valid item template", type.c_str(), display_name.c_str() );
    }
}

void item_requirement::load(JsonObject &jsobj)
{
    type = jsobj.get_string("item");
    if(jsobj.has_int("charges")) {
        count = jsobj.get_int("charges");
        by_charges = true;
    } else if(jsobj.has_int("count")) {
        count = jsobj.get_int("count");
        by_charges = false;
    } else {
        count = 0; by_charges = false;
    }

    recoverable = jsobj.has_bool("recoverable") ? jsobj.get_bool("recoverable") : true;
}

item_requirement item_requirement::from_json(JsonObject &jsobj)
{
    item_requirement obj = item_requirement();
    obj.load(jsobj);
    return obj;
}

bool item_requirement::has( const inventory &crafting_inv, int batch ) const
{
    if(count == 0) {
        if( type == "goggles_welding" ) {
            if( g->u.has_bionic( "bio_sunglasses" ) || g->u.is_wearing( "rm13_armor_on" ) ) {
                return true;
            }
        }

        return crafting_inv.has_tools( type, 1 );
    } else if(by_charges) {
        return crafting_inv.has_charges( type, count * batch );
    } else {
        // If you've Rope Webs, you can spin up the webbing to replace any amount of
        // rope your projects may require.  But you need to be somewhat nourished:
        // Famished or worse stops it.
        if( type == "rope_30" || type == "rope_6" ) {
            // NPC don't craft?
            // TODO: what about the amount of ropes vs the hunger?
            // TODO: This could be changed into a recipe with a mutation requirement someday
            if( g->u.has_trait( "WEB_ROPE" ) && g->u.hunger <= 300 ) {
                return true;
            }
        }
        return crafting_inv.has_components( type, count * batch );
    }
}

std::string item_requirement::to_string(int batch) const
{
    if(count == 0) {
        return item::nname(type, 1);
    } else if( by_charges ) {
        //~ <tool-name> (<numer-of-charges> charges)
        return string_format( ngettext( "%s (%d charge)", "%s (%d charges)", count * batch ),
                              item::nname( type ).c_str(), count * batch );
    } else {
        const int c = count * batch;
        //~ <item-count> <item-name>
        return string_format( ngettext( "%d %s", "%d %s", c ), c, item::nname( type, c ).c_str() );
    }
}

std::string item_requirement::get_color( bool has_one, const inventory &crafting_inv, int batch ) const
{
    if(count == 0) {
        if( type == "goggles_welding" ) {
            if( g->u.has_bionic( "bio_sunglasses" ) || g->u.is_wearing( "rm13_armor_on" ) ) {
                return "cyan";
            }
        }
        if( available == a_insufficent ) { return "brown"; }
        if( crafting_inv.has_tools(type, 1) ) {
            return "green";
        }
    } else if(by_charges) {
        if( available == a_insufficent ) { return "brown"; }
        if ( crafting_inv.has_charges(type, count * batch) ) {
            return "green";
        }
    } else {
        if( type == "rope_30" || type == "rope_6" ) {
            if( g->u.has_trait( "WEB_ROPE" ) && g->u.hunger <= 300 ) {
                return "ltgreen"; // Show that WEB_ROPE is on the job!
            }
        }
        if( available == a_insufficent ) { return "brown"; }

        if( crafting_inv.has_components( type, count * batch ) ) {
            return "green";
        }
    }

    return has_one ? "dkgray" : "red";
}

template<typename T>
const T *requirement_data::find_by_type(const std::vector< std::vector<T> > &vec,
                                    const std::string &type)
{
    for( const auto &list : vec) {
        for( const auto &comp : list) {
            if( comp.type == type ) {
                return &comp;
            }
        }
    }
    return nullptr;
}

bool requirement_data::check_enough_materials( const inventory &crafting_inv, int batch ) const
{
    bool retval = true;
    for( const auto &component_choices : components ) {
        bool atleast_one_available = false;
        for( const auto &comp : component_choices ) {
            if( check_enough_materials( comp, crafting_inv, batch ) ) {
                atleast_one_available = true;
            }
        }
        if( !atleast_one_available ) {
            retval = false;
        }
    }
    return retval;
}

bool requirement_data::check_enough_materials( const item_requirement &comp,
        const inventory &crafting_inv, int batch ) const
{
    if( comp.available != a_true ) {
        return false;
    }
    const int count = comp.count * batch;
    const item_requirement *tq = find_by_type( tools, comp.type );
    if( tq != nullptr && tq->available == a_true ) {
        // The very same item type is also needed as tool!
        // Use charges of it, or use it by count?
        const int tc = tq->by_charges ? 1 : tq->count;
        // Check for components + tool count. Check item amount (excludes
        // pseudo items) and tool amount (includes pseudo items)
        // Imagine: required = 1 welder (component) + 1 welder (tool),
        // available = 1 welder (real item), 1 welding rig (creates
        // a pseudo welder item). has_components(welder,2) returns false
        // as there is only one real welder available, but has_tools(welder,2)
        // returns true.
        // Keep in mind that both requirements (tool+component) are checked
        // before this. That assures that one real item is actually available,
        // two welding rigs (and no real welder) would make this component
        // non-available even before this function is called.
        // Only ammo and (some) food is counted by charges, both are unlikely
        // to appear as tool, but it's possible /-:
        const item_requirement i_tmp( comp.type, count + tc, false );
        const item_requirement t_tmp( comp.type, count + tc, true ); // not by charges!
        // batch factor is explicitly 1, because it's already included in the count.
        if( !i_tmp.has( crafting_inv, 1 ) && !t_tmp.has( crafting_inv, 1 ) ) {
            comp.available = a_insufficent;
        }
    }
    const itype *it = item::find_type( comp.type );
    for( const auto &ql : it->qualities ) {
        const quality_requirement *qr = find_by_type( qualities, ql.first );
        if( qr == nullptr || qr->level > ql.second ) {
            continue;
        }
        // This item can be used for the quality requirement, same as above for specific
        // tools applies.
        if( !crafting_inv.has_items_with_quality( qr->type, qr->level, 1 + comp.count ) ) {
            comp.available = a_insufficent;
        }
    }
    return comp.available == a_true;
}

template<typename T>
bool requirement_data::remove_item( const std::string &type, std::vector< std::vector<T> > &vec )
{
    for( auto &elem : vec ) {
        for( auto c = elem.begin(); c != elem.end(); ) {
            if( c->type == type ) {
                if( elem.size() == 1 ) {
                    return true;
                }
                c = elem.erase( c );
            } else {
                ++c;
            }
        }
    }
    return false;
}

bool requirement_data::remove_item( const std::string &type )
{
    return remove_item( type, tools ) || remove_item( type, components );
}
