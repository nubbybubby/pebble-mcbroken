module.exports = [
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "mcsave slots"
      },
      {
        "type": "text",
        "defaultValue": "Type in the street address of a location as it's shown on <a href=\"https://mcbroken.com\" style=\"color: #FFAA00;\">mcbroken.com</a> into one of the text fields.",
      },
      {
        "type": "input",
        "messageKey": "mc_save_slot_1",
        "attributes": {
          "type": "mc_saved_locations",
          "limit": 90
        }
      },
      {
        "type": "input",
        "messageKey": "mc_save_slot_2",
        "attributes": {
          "type": "mc_saved_locations",
          "limit": 90
        }
      },
      {
        "type": "input",
        "messageKey": "mc_save_slot_3",
        "attributes": {
          "type": "mc_saved_locations",
          "limit": 90
        }
      },
      {
        "type": "input",
        "messageKey": "mc_save_slot_4",
        "attributes": {
          "type": "mc_saved_locations",
          "limit": 90
        }
      },
      {
        "type": "input",
        "messageKey": "mc_save_slot_5",
        "attributes": {
          "type": "mc_saved_locations",
          "limit": 90
        }
      },
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save settings"
  }
];
