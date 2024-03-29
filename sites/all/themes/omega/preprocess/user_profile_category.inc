<?php

/**
 * Implements hook_preprocess_user_profile_category().
 *
 * Backports the following changes to made Drupal 8:
 * - #1190218: Convert user-profile-category.tpl.php to HTML5.
 */
function omega_preprocess_user_profile_category(&$variables) {
  $variables['attributes_array']['class'][] = 'user-profile-category-' . drupal_html_class($variables['title']);
}
