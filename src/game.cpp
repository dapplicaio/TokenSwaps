#include <game.hpp>

void game::receive_asset_transfer
(
  const name& from,
  const name& to,
  std::vector <uint64_t>& asset_ids,
  const std::string& memo
)
{
  if(to != get_self())
    return;

  if(memo == "stake farming item")
  {
    check(asset_ids.size() == 1, "You must transfer only one farming item to stake");
    stake_farmingitem(from, asset_ids[0]);
  }
  else if(memo.find("stake items:") != std::string::npos)
  {
    const uint64_t farmingitem_id = std::stoll(memo.substr(12));
    stake_items(from, farmingitem_id, asset_ids);
  }
  else if(memo.find("blend:") != std::string::npos)
  {
    const uint64_t blend_id = std::stoll(memo.substr(6));
    blend(from, asset_ids, blend_id);
  }
  else
    check(0, "Invalid memo");
  
}


void game::stake_farmingitem(const name& owner, const uint64_t& asset_id)
{
    auto assets       = atomicassets::get_assets(get_self());
    auto asset_itr    = assets.find(asset_id);

    auto farmingitem_mdata = get_mdata(asset_itr);
    if(farmingitem_mdata.find("slots") == std::end(farmingitem_mdata))
    {
        auto farmingitem_template_idata = get_template_idata(asset_itr->template_id, asset_itr->collection_name);
        check(farmingitem_template_idata.find("maxSlots") != std::end(farmingitem_template_idata),
            "Farming item slots was not initialized. Contact ot dev team");
        check(farmingitem_template_idata.find("stakeableResources") != std::end(farmingitem_template_idata),
            "stakeableResources items at current farming item was not initialized. Contact to dev team");

        farmingitem_mdata["slots"] = (uint8_t)1;
        farmingitem_mdata["level"] = (uint8_t)1;

        update_mdata(asset_itr, farmingitem_mdata, get_self());
    }
    
    staked_t staked_table(get_self(), owner.value);
    staked_table.emplace(get_self(), [&](auto &new_row)
    {
        new_row.asset_id = asset_id;
    });
}


void game::stake_items(const name& owner, const uint64_t& farmingitem, const std::vector<uint64_t>& items_to_stake)
{
    auto assets       = atomicassets::get_assets(get_self());

    staked_t staked_table(get_self(), owner.value);
    auto staked_table_itr = staked_table.require_find(farmingitem, "Could not find farming staked item");
    auto asset_itr = assets.find(farmingitem);

    auto farmingitem_mdata          = get_mdata(asset_itr);
    auto farmingitem_template_idata = get_template_idata(asset_itr->template_id, asset_itr->collection_name); 

    check(std::get<uint8_t>(farmingitem_mdata["slots"]) >= staked_table_itr->staked_items.size() + items_to_stake.size(),
     "You don't have empty slots on current farming item to stake this amount of items");

    atomicdata::string_VEC stakeableResources = std::get<atomicdata::string_VEC>(farmingitem_template_idata["stakeableResources"]);
    for(const uint64_t& item_to_stake : items_to_stake)
    {
        asset_itr = assets.find(item_to_stake);
        auto item_mdata = get_mdata(asset_itr);

        item_mdata["lastClaim"] = current_time_point().sec_since_epoch();
        auto template_idata = get_template_idata(asset_itr->template_id, asset_itr->collection_name);
        if(item_mdata.find("level") == std::end(item_mdata))
        {
            check(template_idata.find("farmResource") != std::end(template_idata),
                "farmResource at item[" + std::to_string(item_to_stake) + "] was not initialized. Contact to dev team");
            check(template_idata.find("miningRate") != std::end(template_idata),
                "miningRate at item[" + std::to_string(item_to_stake) + "] was not initialized. Contact to dev team");
            check(template_idata.find("maxLevel") != std::end(template_idata),
                "maxLevel at item[" + std::to_string(item_to_stake) + "] was not initialized. Contact to dev team");
            
            item_mdata["level"] = (uint8_t)1;
        }

        check(std::find(std::begin(stakeableResources), std::end(stakeableResources), std::get<std::string>(template_idata["farmResource"])) != std::end(stakeableResources),
            "Item [" + std::to_string(item_to_stake) + "] can not be staked at current farming item");
        update_mdata(asset_itr, item_mdata, get_self());
    }

    staked_table.modify(staked_table_itr, get_self(), [&](auto &new_row)
    {
        new_row.staked_items.insert(std::end(new_row.staked_items), std::begin(items_to_stake), std::end(items_to_stake));
    });
}


void game::claim(const name& owner, const uint64_t& farmingitem)
{
    require_auth(owner);

    staked_t staked_table(get_self(), owner.value);
    auto staked_table_itr = staked_table.require_find(farmingitem, "Could not find staked farming item");
    auto assets = atomicassets::get_assets(get_self());
    auto assets_itr = assets.find(farmingitem);

    //to get mining boost
    auto farmingitem_mdata = get_mdata(assets_itr);
    float miningBoost = 1;
    if(farmingitem_mdata.find("miningBoost") != std::end(farmingitem_mdata))
        miningBoost = std::get<float>(farmingitem_mdata["miningBoost"]);
    
    // first - resource name, second - resource amount
    std::map<std::string, float> mined_resources;
    const uint32_t& time_now = current_time_point().sec_since_epoch();
    for(const uint64_t& item_to_collect : staked_table_itr->staked_items)
    {
        auto assets_itr           = assets.find(item_to_collect);
        const std::pair<std::string, float> item_reward = claim_item(assets_itr, 2, time_now); // 2 is the percentage of increase in mine rate for each level

        if(item_reward != std::pair<std::string,float>())
            if(item_reward.second > 0)
                mined_resources[item_reward.first] += item_reward.second;
    }  
    check(mined_resources.size() > 0, "Nothing to claim");

    increase_owner_resources_balance(owner, mined_resources);
}

void game::upgradeitem(
    const name& owner,
    const uint64_t& item_to_upgrade,
    const uint8_t& next_level,
    const uint64_t& staked_at_farmingitem
)
{
    require_auth(owner);

    const int32_t& time_now = current_time_point().sec_since_epoch();

    auto assets     = atomicassets::get_assets(get_self());
    auto asset_itr  = assets.require_find(item_to_upgrade, ("Could not find staked item[" + std::to_string(item_to_upgrade) +"]").c_str());

    staked_t staked_table(get_self(), owner.value);
    auto staked_table_itr = staked_table.require_find(staked_at_farmingitem, "Could not find staked farming item");

    check(std::find(std::begin(staked_table_itr->staked_items), std::end(staked_table_itr->staked_items), item_to_upgrade) != std::end(staked_table_itr->staked_items),
        "Item [" + std::to_string(item_to_upgrade) + "] is not staked at farming item");

    //claiming mined resources before upgrade
    const std::pair<std::string, float> item_reward = claim_item(asset_itr, 2, time_now); // 2 is the percentage of increase in mine rate for each level
    if(item_reward != std::pair<std::string,float>())
    {
        if(item_reward.second > 0)
        {
            increase_owner_resources_balance(owner, std::map<std::string, float>({item_reward}));
        }
    }
    // upgrading
    upgrade_item(asset_itr, 2, owner, next_level, time_now); // 2 is the percentage of increase in mine rate for each level
}

void game::upgrade_item(
  atomicassets::assets_t::const_iterator& assets_itr,
  const uint8_t& upgrade_percentage,
  const name& owner,
  const uint8_t& new_level,
  const uint32_t& time_now
)
{
  auto mdata          = get_mdata(assets_itr);
  auto template_idata = get_template_idata(assets_itr->template_id, assets_itr->collection_name);

  const float& mining_rate   = std::get<float>(template_idata["miningRate"]);
  const uint8_t& current_lvl = std::get<uint8_t>(mdata["level"]);
  const std::string& resource_name = std::get<std::string>(template_idata["farmResource"]);
  check(current_lvl < new_level, "New level must be higher then current level");
  check(new_level <= std::get<uint8_t>(template_idata["maxLevel"]), "New level can not be higher then max level");
  check(std::get<uint32_t>(mdata["lastClaim"]) < time_now, "Item is upgrading");

  float miningRate_according2lvl = mining_rate;
  for(uint8_t i = 1; i < new_level; ++i)
    miningRate_according2lvl = miningRate_according2lvl + (miningRate_according2lvl * upgrade_percentage / 100);

 
  const int32_t& upgrade_time  = get_upgrading_time(new_level) - get_upgrading_time(current_lvl);
  const float& resource_price = upgrade_time * miningRate_according2lvl;

  std::get<uint8_t>(mdata["level"]) = new_level;
  std::get<uint32_t>(mdata["lastClaim"]) = time_now + upgrade_time;

  reduce_owner_resources_balance(owner, std::map<std::string, float>({{resource_name, resource_price}}));

  update_mdata(assets_itr, mdata, get_self());
}

const int32_t game::get_upgrading_time(const uint8_t& end_level)
{ 
  const int32_t increasing_time = 320; // 5.33 min
  int32_t total_time = 0;
  int32_t temp_tracker = 0;
  for(uint8_t i = 2; i <= end_level; ++i)
  {
    if(i % 5 == 0)
    {
      total_time += (temp_tracker * 5);
    }
    else
    {
      temp_tracker += increasing_time;
      total_time += increasing_time;
    }
  }
  return total_time;
}

void game::upgfarmitem(const name& owner, const uint64_t& farmingitem_to_upgrade, const bool& staked)
{
    require_auth(owner);

    if(staked)
    {
        auto assets     = atomicassets::get_assets(get_self());
        auto asset_itr  = assets.require_find(farmingitem_to_upgrade, ("Could not find staked item[" + std::to_string(farmingitem_to_upgrade) +"]").c_str());

        staked_t staked_table(get_self(), owner.value);
        staked_table.require_find(farmingitem_to_upgrade, "Could not find staked farming item");

        upgrade_farmingitem(asset_itr, get_self());
    }
    else
    {
        auto assets     = atomicassets::get_assets(owner);
        auto asset_itr  = assets.require_find(farmingitem_to_upgrade, ("You do not own farmingitem[" + std::to_string(farmingitem_to_upgrade) +"]").c_str());
        upgrade_farmingitem(asset_itr, owner);
    }
}

void game::upgrade_farmingitem(atomicassets::assets_t::const_iterator& assets_itr, const name& owner)
{
  auto mdata          = get_mdata(assets_itr);
  auto template_idata = get_template_idata(assets_itr->template_id, assets_itr->collection_name);

  check(std::get<uint8_t>(mdata["slots"])++ < std::get<uint8_t>(template_idata["maxSlots"]), "Farmingitem has max slots");

  update_mdata(assets_itr, mdata, owner);
}

void game::addblend(
  const std::vector<int32_t>  blend_components,
  const int32_t resulting_item
)
{
  require_auth(get_self());

  blends_t blends_table(get_self(), get_self().value);

  const uint64_t new_blend_id = blends_table.available_primary_key();
  
  blends_table.emplace(get_self(), [&](auto new_row)
  {
    new_row.blend_id = new_blend_id;
    new_row.blend_components = blend_components;
    new_row.resulting_item = resulting_item;
  });

}

void game::blend(const name& owner, const std::vector<uint64_t> asset_ids, const uint64_t& blend_id)
{
    auto assets = atomicassets::get_assets(get_self());
    auto templates = atomicassets::get_templates(get_self());
    blends_t blends_table(get_self(), get_self().value);
    auto blends_table_itr = blends_table.require_find(blend_id, "Could not find blend id");
    check(blends_table_itr->blend_components.size() == asset_ids.size(), "Blend components count mismatch");

    std::vector<int32_t> temp = blends_table_itr->blend_components;
    for(const uint64_t& asset_id : asset_ids)
    {
        auto assets_itr = assets.find(asset_id);
        check(assets_itr->collection_name == name("collname"), // replace collection with your collection name to check for fake nfts
         ("Collection of asset [" + std::to_string(asset_id) + "] mismatch").c_str());
        auto found =  std::find(std::begin(temp), std::end(temp), assets_itr->template_id);
        if(found != std::end(temp))
            temp.erase(found);
        
        action
        (
            permission_level{get_self(),"active"_n},
            atomicassets::ATOMICASSETS_ACCOUNT,
            "burnasset"_n,
            std::make_tuple
            (
                get_self(),
                asset_id
            )
        ).send();
    }
    check(temp.size() == 0, "Invalid blend components");

    auto templates_itr = templates.find(blends_table_itr->resulting_item);

    action
    (
        permission_level{get_self(),"active"_n},
        atomicassets::ATOMICASSETS_ACCOUNT,
        "mintasset"_n,
        std::make_tuple
        (
            get_self(),
            get_self(),
            templates_itr->schema_name,
            blends_table_itr->resulting_item,
            owner,
            (atomicassets::ATTRIBUTE_MAP) {}, //immutable_data
            (atomicassets::ATTRIBUTE_MAP) {}, //mutable data
            (std::vector <asset>) {} // token back
        )
    ).send();
}

void game::setratio(const std::string& resource, const float& ratio)
{
  require_auth(get_self());

  const uint64_t key_id = stringToUint64(resource);
  resourcecost_t resourcecost_table(get_self(), get_self().value);
  auto resourcecost_table_itr = resourcecost_table.find(key_id);

  if(resourcecost_table_itr == std::end(resourcecost_table))
  {
    resourcecost_table.emplace(get_self(), [&](auto &new_row)
    {
      new_row.key_id = key_id;
      new_row.resource_name = resource;
      new_row.ratio = ratio;
    });
  }
  else
  {
    resourcecost_table.modify(resourcecost_table_itr, get_self(), [&](auto &new_row)
    {
      new_row.resource_name = resource;
      new_row.ratio = ratio;
    });
  }
}

void game::swap(const name& owner, const std::string& resource, const float& amount2swap)
{
    require_auth(owner);

    resourcecost_t resourcecost_table(get_self(), get_self().value);
    auto resourcecost_table_itr = resourcecost_table.require_find(stringToUint64(resource), "Could not find resource cost config");

    const float token_amount = amount2swap / resourcecost_table_itr->ratio;
    const asset tokens2receive = asset(token_amount * 10000, symbol("GAME", 4)); // change to token you have deployed
    
    reduce_owner_resources_balance(owner, std::map<std::string, float>({{resource, amount2swap}}));
    tokens_transfer(owner, tokens2receive);
}

void game::tokens_transfer(const name& to, const asset& quantity)
{

  action
  (
    permission_level{get_self(),"active"_n},
    "tokencontr"_n, // change to your deployed token contract
    "transfer"_n,
    std::make_tuple
    (
      get_self(),
      to,
      quantity,
      std::string("")
    )
  ).send();
}

const std::pair<std::string, float> game::claim_item(atomicassets::assets_t::const_iterator& assets_itr, const uint8_t& upgrade_percentage, const uint32_t& time_now)
{
  auto item_mdata           = get_mdata(assets_itr);
  const uint32_t& lastClaim = std::get<uint32_t>(item_mdata["lastClaim"]);
  std::pair<std::string, float> mined_resource;

  if(time_now > lastClaim)
  {
    auto item_template_idata        = get_template_idata(assets_itr->template_id, assets_itr->collection_name);
    const float& miningRate         = std::get<float>(item_template_idata["miningRate"]);
    const std::string& farmResource = std::get<std::string>(item_template_idata["farmResource"]);
    const uint8_t&  current_lvl     = std::get<uint8_t>(item_mdata["level"]);

    //calculate mining rate according to lvl
    float miningRate_according2lvl = miningRate;
    for(uint8_t i = 1; i < current_lvl; ++i)
        miningRate_according2lvl = miningRate_according2lvl + (miningRate_according2lvl * upgrade_percentage / 100);

    const float& reward = (time_now - lastClaim) * miningRate_according2lvl;
    item_mdata["lastClaim"] = time_now;
    update_mdata(assets_itr, item_mdata, get_self());

    mined_resource.first = farmResource;
    mined_resource.second = reward;
  }

  return mined_resource;
}

void game::increase_owner_resources_balance(const name& owner, const std::map<std::string, float>& resources)
{
  resources_t resources_table(get_self(), owner.value);
  for(const auto& map_itr : resources)
  {
    const uint64_t& key_id = stringToUint64(map_itr.first);

    auto resources_table_itr = resources_table.find(key_id);
    if(resources_table_itr == std::end(resources_table))
    {
      resources_table.emplace(get_self(), [&](auto &new_row)
      {
        new_row.key_id          = key_id;
        new_row.resource_name   = map_itr.first;
        new_row.amount          = map_itr.second;
      });
    }
    else
    {
      resources_table.modify(resources_table_itr, get_self(), [&](auto &new_row)
      {
        new_row.amount += map_itr.second;
      });
    }
  }
}

void game::reduce_owner_resources_balance(const name& owner, const std::map<std::string, float>& resources)
{
  resources_t resources_table(get_self(), owner.value);

  for(const auto& map_itr : resources)
  {
    const uint64_t& key_id = stringToUint64(map_itr.first);
    auto resources_table_itr = resources_table.require_find(key_id,
      ("Could not find balance of " + map_itr.first).c_str());
    check(resources_table_itr->amount >= map_itr.second, ("Overdrawn balance: " + map_itr.first).c_str());

    if(resources_table_itr->amount == map_itr.second)
      resources_table.erase(resources_table_itr);
    else
    {
      resources_table.modify(resources_table_itr, get_self(), [&](auto &new_row)
      {
        new_row.amount -= map_itr.second;
      });
    }
  }
}


atomicassets::ATTRIBUTE_MAP game::get_mdata(atomicassets::assets_t::const_iterator& assets_itr)
{
  auto schemas = atomicassets::get_schemas(assets_itr->collection_name);
  auto schema_itr = schemas.find(assets_itr->schema_name.value);

  atomicassets::ATTRIBUTE_MAP deserialized_mdata = atomicdata::deserialize             
  (
    assets_itr->mutable_serialized_data,
    schema_itr->format
  );

  return deserialized_mdata;
}

atomicassets::ATTRIBUTE_MAP game::get_template_idata(const int32_t& template_id, const name& collection_name)
{
  auto templates = atomicassets::get_templates(collection_name);
  auto template_itr = templates.find(template_id);

  auto schemas = atomicassets::get_schemas(collection_name);
  auto schema_itr = schemas.find(template_itr->schema_name.value);

  return atomicdata::deserialize             
  (
    template_itr->immutable_serialized_data,
    schema_itr->format
  );
}

void game::update_mdata(atomicassets::assets_t::const_iterator& assets_itr, const atomicassets::ATTRIBUTE_MAP& new_mdata, const name& owner)
{
  action
  (
    permission_level{get_self(),"active"_n},
    atomicassets::ATOMICASSETS_ACCOUNT,
    "setassetdata"_n,
    std::make_tuple
    (
      get_self(),
      owner,
      assets_itr->asset_id,
      new_mdata
    )
  ).send();
}


const uint64_t game::stringToUint64(const std::string& str)
{
  uint64_t hash = 0;
    
  if (str.size() == 0) return hash;
    
  for (int i = 0; i < str.size(); ++i) 
  {
    int char_s = str[i];
    hash = ((hash << 4) - hash) + char_s;
    hash = hash & hash;
  }
    
  return hash;
}





