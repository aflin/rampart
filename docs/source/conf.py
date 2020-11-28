# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
# import os
# import sys
# sys.path.insert(0, os.path.abspath('.'))


# -- Project information -----------------------------------------------------

project = 'Rampart'
copyright = '2020, Moat Crossing Systems'
author = 'Moat Crossing Systems'

# The full version, including alpha/beta/rc tags
release = '0.1.0'


# -- General configuration ---------------------------------------------------

master_doc = 'index'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
autosectionlabel_prefix_document = True
extensions = [
    'sphinx.ext.autosectionlabel'
]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = []


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
#html_theme = 'classic'

import sphinx_theme
html_theme = "neo_rtd_theme"
html_theme_path = [sphinx_theme.get_html_theme_path('neo_rtd_theme')]

#html_theme = 'rtcat_sphinx_theme'
#import rtcat_sphinx_theme
#html_theme_path = [rtcat_sphinx_theme.get_html_theme_path()]

pygments_style = 'sphinx'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']
# ADDED to allow wrapping in tables: 
# https://rackerlabs.github.io/docs-rackspace/tools/rtd-tables.html
# -ajf
html_context = {
    'css_files': [
        '_static/theme_overrides.css',  # override wide tables in RTD theme
        '_static/hacks.css' #colors from https://bitbucket.org/lbesson/web-sphinx/src/master/.static/hacks.css
        ],
     }
#html_theme_options = {
#    'canonical_url': '',
#    'analytics_id': 'UA-XXXXXXX-1',  #  Provided by Google in your dashboard
#    'logo_only': False,
#    'display_version': True,
#    'prev_next_buttons_location': 'bottom',
#    'style_external_links': False,
#    'vcs_pageview_mode': '',
#    'style_nav_header_background': 'white',
    # Toc options
# navigation depth 5 doesn't seem to work with
# preferred themes
html_theme_options = {
    'display_version': False,
    'collapse_navigation': True,
    'sticky_navigation': True,
    'navigation_depth': 4,
    'includehidden': True,
    'titles_only': False
}

rst_prolog = """
.. include:: special.rst
"""
