#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/asset.hpp>
#include "atomicassets.hpp"


using namespace eosio;

class [[eosio::contract]] game : public contract 
{
  public:
    using contract::contract;


    // listening atomicassets transfer
    [[eosio::on_notify("atomicassets::transfer")]] 
    void receive_asset_transfer
    (
      const name& from,
      const name& to,
      std::vector <uint64_t>& asset_ids,
      const std::string& memo
    );


    [[eosio::action]]
    void claim(const name& owner, const uint64_t& farmingitem);

    [[eosio::action]]
    void upgradeitem(
      const name& owner,
      const uint64_t& item_to_upgrade,
      const uint8_t& next_level,
      const uint64_t& staked_at_farmingitem
    );


    [[eosio::action]]
    void upgfarmitem(const name& owner, const uint64_t& farmingitem_to_upgrade, const bool& staked);


    [[eosio::action]]
    void addblend(
      const std::vector<int32_t>  blend_components,
      const int32_t resulting_item
    );

    [[eosio::action]]
    void setratio(const std::string& resource, const float& ratio);


    [[eosio::action]]
    void swap(const name& owner, const std::string& resource, const float& amount2swap);


  private:

  //scope: owner
  struct [[eosio::table]] staked_j
  {
    uint64_t              asset_id; // item
    std::vector<uint64_t> staked_items; // farming items

    uint64_t primary_key() const { return asset_id; }
  };
  typedef multi_index< "staked"_n, staked_j > staked_t;

  //scope: owner
  struct [[eosio::table]] resources_j
  {
    uint64_t    key_id;
    float       amount;
    std::string resource_name;

    uint64_t primary_key() const { return key_id; }
  };
  typedef multi_index< "resources"_n, resources_j > resources_t;

  //scope:contract
  struct [[eosio::table]] blends_j
  {
    uint64_t              blend_id;
    std::vector<int32_t>  blend_components;
    int32_t               resulting_item;

    uint64_t primary_key() const { return blend_id; }
  };
  typedef multi_index< "blends"_n, blends_j > blends_t;

  struct [[eosio::table]] resourcecost_j
  {
    uint64_t     key_id;
    std::string  resource_name;
    float        ratio; // if user swap 100 wood and ration is 25 it means that user will receive 4 tokens

    uint64_t primary_key() const { return key_id; }
  };
  typedef multi_index< "resourcecost"_n, resourcecost_j > resourcecost_t;



  const uint64_t stringToUint64(const std::string& str);

  void stake_farmingitem(const name& owner, const uint64_t& asset_id);
  void stake_items(const name& owner, const uint64_t& farmingitem, const std::vector<uint64_t>& items_to_stake);

  void increase_owner_resources_balance(const name& owner, const std::map<std::string, float>& resources);
  void reduce_owner_resources_balance(const name& owner, const std::map<std::string, float>& resources);

  const std::pair<std::string, float> claim_item(atomicassets::assets_t::const_iterator& assets_itr, const uint8_t& upgrade_percentage, const uint32_t& time_now);

  void upgrade_item(
    atomicassets::assets_t::const_iterator& assets_itr,
    const uint8_t& upgrade_percentage,
    const name& owner,
    const uint8_t& new_level,
    const uint32_t& time_now
  );

  void upgrade_farmingitem(atomicassets::assets_t::const_iterator& assets_itr, const name& owner);

  void blend(const name& owner, const std::vector<uint64_t> asset_ids, const uint64_t& blend_id);



  void tokens_transfer(const name& to, const asset& quantity);

  const int32_t get_upgrading_time(const uint8_t& end_level);

  // get mutable data from NFT
  atomicassets::ATTRIBUTE_MAP get_mdata(atomicassets::assets_t::const_iterator& assets_itr);
  // get immutable data from template of NFT
  atomicassets::ATTRIBUTE_MAP get_template_idata(const int32_t& template_id, const name& collection_name);
  // update mutable data of NFT
  void update_mdata(atomicassets::assets_t::const_iterator& assets_itr, const atomicassets::ATTRIBUTE_MAP& new_mdata, const name& owner);
};
